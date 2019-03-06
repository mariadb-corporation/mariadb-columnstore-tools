/*
* Copyright (c) 2019 MariaDB Corporation Ab
*
* Use of this software is governed by the Business Source License included
* in the LICENSE file and at www.mariadb.com/bsl11.
*
* Change Date: 2022-02-15
*
* On the date above, in accordance with the Business Source License, use
* of this software will be governed by version 2 or later of the General
* Public License.
*/

#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <libmcsapi/mcsapi.h>
#include <yaml-cpp/yaml.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

/**
* Helper class to parse the input cmd parameter.
*/
class InputParser {
public:
    InputParser(int &argc, char **argv) {
        for (int i = 1; i < argc; ++i)
            this->tokens.push_back(std::string(argv[i]));
    }
    const std::string& getCmdOption(const std::string &option) const {
        std::vector<std::string>::const_iterator itr;
        itr = std::find(this->tokens.begin(), this->tokens.end(), option);
        if (itr != this->tokens.end() && ++itr != this->tokens.end()) {
            return *itr;
        }
        static const std::string empty_string("");
        return empty_string;
    }
    bool cmdOptionExists(const std::string &option) const {
        return std::find(this->tokens.begin(), this->tokens.end(), option)
            != this->tokens.end();
    }
private:
    std::vector <std::string> tokens;
};

/**
* Thread safe FiFo queue for one consumer and one producer utilising a ring buffer.
*/
template <typename T>
class SharedDataStorage {
public:
    /**
    * Initializes the SharedDataStorage and sets its buffer size and the wait time before the processing is continued if the buffer is full / empty.
    */
    SharedDataStorage(uint32_t bufferSize, uint32_t waitTime) {
        this->bufferSize = bufferSize;
        this->pushWaitTime = waitTime;
        this->popWaitTime = waitTime / 2;
        data = new T[bufferSize];
    }

    /**
    * Pushes an element into the FiFo queue.
    */
    void push(T& t) {
        if ((write_pointer + 1) % bufferSize == read_pointer) {
            // the array is full
            std::unique_lock<std::mutex> guard(m_data);
            c_data.wait(guard, [this] {return (write_pointer + 1) % bufferSize != read_pointer; });
            guard.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(pushWaitTime)); //give it some time to empty before continue
        }
        data[write_pointer] = t;
        write_pointer = ++write_pointer % bufferSize;
        c_data.notify_one();
    }

    /**
    * Pops the latest element out of the FiFo queue.
    * @returns true if there are more elements to process in the queue.
    */
    bool pop(T& t) {
        if (finished) {
            if (read_pointer != write_pointer) {
                // the array is still filled and no more data is expected
                t = data[read_pointer];
                read_pointer = ++read_pointer % bufferSize;
                c_data.notify_one();
                return true;
            }
            else {
                // the array is empty and no more data is expected
                return false;
            }
        }
        else {
            if (read_pointer == write_pointer) {
                // the array is empty but more data is expected
                std::unique_lock<std::mutex> guard(m_data);
                c_data.wait(guard, [this] {return (read_pointer != write_pointer) || finished; });
                guard.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(popWaitTime)); // give it some time to fill before continue
                return pop(t);
            }
            else {
                // the array is filled and more date is expected
                t = data[read_pointer];
                read_pointer = ++read_pointer % bufferSize;
                c_data.notify_one();
                return true;
            }
        }
    }

    /**
    * Sets an internal flag to indicate that no more data is injected from the producer.
    */
    void finishedWriting() {
        finished = true;
        c_data.notify_one();
    }

    /**
    * Empties the shared data storage on deconstruction.
    */
    ~SharedDataStorage() {
        delete[] data;
    }

private:
    T* data;
    std::mutex m_data;
    std::condition_variable c_data;
    bool finished = false;
    uint32_t bufferSize;
    uint32_t popWaitTime;
    uint32_t pushWaitTime;
    uint32_t write_pointer = 0;
    uint32_t read_pointer = 0;
};

/**
* CSV remote import class. Injects a CSV file into MariaDB ColumnStore.
* Internally it ueses a pipeline of three threads and two FiFo queues for the processing. 
* One thread for reading the csv file, one thread for parsing it into fields, and one thread for writing the parsed fields to ColumnStore.
*/
class MCSRemoteImport {
public:
    /**
    * Instantiates a new remote import object, and checks if the given parameter are ok.
    */
    MCSRemoteImport(std::string input_file, std::string database, std::string table, std::string mapping_file, std::string columnStoreXML, char delimiter, std::string inputDateFormat, bool default_non_mapped, char escape_character, char enclose_by_character, bool header, bool error_log, std::int32_t nullOption, bool ignore_malformed_csv, uint32_t file_input_buffer_size, uint32_t file_input_buffer_wait_time, uint32_t csv_fields_buffer_size, uint32_t csv_fields_buffer_wait_time, uint32_t read_cache_size) {
        // check if we can connect to the ColumnStore database and extract the number of columns of the target table
        try {
            if (columnStoreXML == "") {
                this->driver = new mcsapi::ColumnStoreDriver();
            }
            else {
                this->driver = new mcsapi::ColumnStoreDriver(columnStoreXML);
            }
            this->cat = driver->getSystemCatalog();
            this->tab = cat.getTable(database, table);
            this->number_of_cs_table_columns = tab.getColumnCount();
            for (uint32_t c = 0; c < number_of_cs_table_columns; c++) {
                this->columnstore_column_types[c] = tab.getColumn(c).getType();
            }
            this->bulk = driver->createBulkInsert(database, table, 0, 0);
        }
        catch (mcsapi::ColumnStoreError &e) {
            std::cerr << "Error during mcsapi initialization: " << e.what() << std::endl;
            clean();
            std::exit(2);
        };

        // check if delimiter and escape_character differ, and delimiter and enclose_by_character differ
        if (delimiter == escape_character || delimiter == enclose_by_character) {
            std::cerr << "Error: Different values need to be chosen for delimiter and enclose_by_character, and delimiter and escape_character" << std::endl;
            std::cerr << "delimiter: " << delimiter << std::endl;
            std::cerr << "enclose_by_character: " << enclose_by_character << std::endl;
            std::cerr << "escape_character: " << escape_character << std::endl;
            clean();
            std::exit(2);
        }

        this->delimiter = delimiter;
        this->escape_character = escape_character;
        this->enclose_by_character = enclose_by_character;
        this->nullOption = nullOption;
        this->ignore_malformed_csv = ignore_malformed_csv;
        this->read_cache_size = read_cache_size;

        // check if the source csv file exists and extract the number of columns of its first row
        std::ifstream csvFile(input_file);
        if (!csvFile) {
            std::cerr << "Error: Can't open input file " << input_file << std::endl;
            clean();
            std::exit(2);
        }
        // if the csv file is smaller than the input file buffer, use the csv file size as input file buffer size and read_cache_size
        csvFile.seekg(0, std::ios::end);
        this->input_file_size = csvFile.tellg();
        if (file_input_buffer_size > input_file_size) {
            file_input_buffer_size = input_file_size;
        }
        if (this->read_cache_size > input_file_size) {
            this->read_cache_size = input_file_size;
        }
        csvFile.seekg(0, std::ios::beg);
        // extract the csv header fields (if any) and number of csv columns from the first line
        std::vector<std::string> csv_header_fields;
        std::string csv_field;
        char c;
        int32_t status = OLD_FIELD;
        bool withinEnclosed = false;
        bool lastCharWasEscapechar = false;
        while (csvFile.get(c) && status != NEW_LINE) {
            reprocess:
            status = processChracterToCsvField(c, csv_field, withinEnclosed, lastCharWasEscapechar);
            switch (status) {
            case NEW_FIELD:
                csv_header_fields.push_back(csv_field);
                csv_field.clear();
                break;

            case NEW_LINE:
                csv_header_fields.push_back(csv_field);
                break;

            case RE_PROCESS_CHARACTER:
                goto reprocess;
            }
        }
        if (csvFile.is_open()) {
            csvFile.close();
        }
        this->number_of_csv_columns = csv_header_fields.size();
        this->input_file = input_file;
        this->inputDateFormat = inputDateFormat;
        this->header = header;

        // check if there is no logging file and if mcsimport is able to create one
        if (error_log) {
            std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
            std::string errFile = input_file + "." + std::to_string(ms.count()) + ".err";
            this->errFileStream.open(errFile);
            if (!this->errFileStream) {
                std::cerr << "Error: Can't write to error file: " << errFile << std::endl;
                clean();
                std::exit(2);
            }
            this->errFileStream << "error_type, column_nr, parsed_raw_row_values" << std::endl;
        }
        this->error_log = error_log;

        if (mapping_file == "") { // if no mapping file was provided use implicit mapping of columnstore_column to csv_column
            generateImplicitMapping(this->number_of_csv_columns, default_non_mapped);
        }
        else { // if a mapping file was provided infer the mapping from the mapping file
            generateExplicitMapping(this->number_of_csv_columns, default_non_mapped, mapping_file);
        }

        this->file_input_buffer = new SharedDataStorage<char>(file_input_buffer_size, file_input_buffer_wait_time);
        this->csv_fields_buffer = new SharedDataStorage <std::vector<std::string>>(csv_fields_buffer_size, csv_fields_buffer_wait_time);
    }

    /**
    * Destructor, cleans allocated memory on exit.
    */
    ~MCSRemoteImport() {
        clean();
    }

    /**
    * Starts the import process.
    */
    int32_t import() {
        std::thread t1(&MCSRemoteImport::readDataFromFileIntoBuffer, this);
        std::thread t2(&MCSRemoteImport::parseDataFromBuffer, this);
        std::thread t3(&MCSRemoteImport::writeDataToColumnStore, this);
        t1.join();
        t2.join();
        t3.join();

        if (error) {
            return 3;
        }
        return 0;
    }

private:
    ///VARIABLES///

    mcsapi::ColumnStoreDriver* driver = nullptr;
    mcsapi::ColumnStoreBulkInsert* bulk = nullptr;
    mcsapi::ColumnStoreSystemCatalog cat;
    mcsapi::ColumnStoreSystemCatalogTable tab;
    SharedDataStorage<char>* file_input_buffer = nullptr;
    SharedDataStorage<std::vector<std::string>>* csv_fields_buffer = nullptr;
    bool error = false;
    enum processCharacterToCsvFieldReturnValues { OLD_FIELD = 0, NEW_FIELD = 1, NEW_LINE = 2, RE_PROCESS_CHARACTER = 3 };
    char enclose_by_character = '"';
    char escape_character = '"';
    char delimiter = ',';
    std::int32_t nullOption = 0;
    bool header = false;
    bool ignore_malformed_csv = false;
    bool error_log = false;
    uint32_t number_of_cs_table_columns = 0;
    uint32_t number_of_csv_columns = 0;
    uint32_t ignored_malformed_csv_lines = 0;
    uint32_t read_cache_size = 1024 * 1024;
    uint64_t input_file_size = 0;
    std::string input_file;
    std::ofstream errFileStream;
    std::mutex m_errorFileStream;
    std::string inputDateFormat;
    enum mapping_codes { COLUMNSTORE_DEFAULT_VALUE = -1, CUSTOM_DEFAULT_VALUE = -2 };
    std::map<int32_t, int32_t> mapping; // columnstore_column #, csv_column # or item of mapping_codes
    std::map<int32_t, std::string> customInputDateFormat; //columnstore_column #, csv_input_date_format
    std::map<int32_t, std::string> customDefaultValue; // columnstore_column #, custom_default_value
    std::map<int32_t, mcsapi::columnstore_data_types_t> columnstore_column_types; // columnstore_column #, columnstore_column_data_type

    ///THREADS///

    /**
    * Reads the input file and buffers it in file_input_buffer.
    */
    void readDataFromFileIntoBuffer() {
        std::ifstream input_file;
        input_file.open(this->input_file);
        char* read_buffer = new char[read_cache_size];
        input_file.read(read_buffer, read_cache_size);
        while (input_file.good() && !error) {
            for (uint32_t i = 0; i < read_cache_size; i++) {
                file_input_buffer->push(read_buffer[i]);
            }
            input_file.read(read_buffer, read_cache_size);
        }
        for (uint32_t i = 0; i < input_file_size % read_cache_size; i++) {
            file_input_buffer->push(read_buffer[i]);
        }
        file_input_buffer->finishedWriting();
        if (input_file.is_open()) {
            input_file.close();
        }
        delete[] read_buffer;
        std::cout << "file read thread done" << std::endl;
    }

    /**
    * Reads the file data from file_input_buffer, parses it into csv fields, and writes them to csv_fields_buffer.
    */
    void parseDataFromBuffer() {
        char c;
        bool withinEnclosed = false;
        bool lastCharWasEscapeChar = false;
        std::string csv_field;
        csv_field.reserve(1024);
        std::vector<std::string> csv_fields;
        while (file_input_buffer->pop(c) && !error) {
            processCharacter:
            switch (processChracterToCsvField(c, csv_field, withinEnclosed, lastCharWasEscapeChar)) {
            case NEW_FIELD:
                csv_fields.push_back(csv_field);
                csv_field.clear();
                break;
                
            case NEW_LINE:
                csv_fields.push_back(csv_field);
                csv_field.clear();
                // verifies that it is not a malformed csv line (the number of fields matches the number of fields of the first line), then adds it to the next buffer
                verifyAndAddParsedCsvLine(csv_fields);
                csv_fields.clear();
                break;

            case RE_PROCESS_CHARACTER:
                goto processCharacter;
            }
        }

        // the last line mustn't end with \n therefore it has to be written to the buffer.
        if (csv_field.size() > 0) {
            csv_fields.push_back(csv_field);
        }
        else if (csv_fields.size() == number_of_csv_columns - 1) { // catch NULL values as last field
            csv_fields.push_back("");
        }
        if (csv_fields.size() > 0) {
            // verifies that it is not a malformed csv line (the number of fields matches the number of fields of the first line), then adds it to the next buffer
            verifyAndAddParsedCsvLine(csv_fields);
        }
        csv_fields_buffer->finishedWriting();

        std::cout << "csv parse thread done" << std::endl;
    }

    /**
    * Reads the csv fields from csv_fields_buffer and writes them to ColumnStore.
    */
    void writeDataToColumnStore() {
        std::vector<std::string> csv_fields;
        bool headerIgnored = false;
        try {
            while (csv_fields_buffer->pop(csv_fields) && !error) {
                if (header && !headerIgnored) {
                    headerIgnored = true;
                }
                else {
                    writeCsvFieldsToColumnStoreTable(csv_fields);
                }
            }
            if (!error) {
                bulk->commit();
            }
        }
        catch (std::exception& e) {
            std::cerr << "Error during mcsapi bulk operation: " << e.what() << std::endl;
            error = true;
        }

        if (error) {
            bulk->rollback();
            std::cerr << "Rollback performed." << std::endl;
        } 
        else {
            // print out the CS injection summary
            mcsapi::ColumnStoreSummary& sum = bulk->getSummary();
            std::cout << "Execution time: " << sum.getExecutionTime() << "s" << std::endl;
            std::cout << "Rows inserted: " << sum.getRowsInsertedCount() << std::endl;
            std::cout << "Truncation count: " << sum.getTruncationCount() << std::endl;
            std::cout << "Saturated count: " << sum.getSaturatedCount() << std::endl;
            std::cout << "Invalid count: " << sum.getInvalidCount() << std::endl;
            if (ignore_malformed_csv) {
                std::cout << "Ignored malformed csv count: " << ignored_malformed_csv_lines << std::endl;
            }
        }

        std::cout << "ColumnStore export thread done" << std::endl;
    }


    ///FUNCTIONS///

    /**
    * Helper function, that helps to process a character into a csv field.
    *
    * Returns:
    * - OLD_FIELD if the character belogs to the old field and was added to it.
    * - NEW_FIELD if the character indicates that a new field begun.
    * - NEW_LINE if the character indicates that a new csv line begun.
    * - RE_PROCESS_CHARACTER if the character needs to be processed again.
    */
    int32_t processChracterToCsvField(char& c, std::string& csv_field, bool& withinEnclosed, bool& lastCharWasEscapeChar) {
        if (withinEnclosed) {
            if (lastCharWasEscapeChar) {
                // enclose by character found
                if (c == enclose_by_character) {
                    csv_field.push_back(enclose_by_character);
                }
                // escape character found
                else if (c == escape_character) {
                    csv_field.push_back(escape_character);
                }
                // in case enclose by and escape character are the same and no second enclose by char was found we have to end the enclosed by here
                else if (enclose_by_character == escape_character) {
                    withinEnclosed = false;
                    lastCharWasEscapeChar = false;
                    return RE_PROCESS_CHARACTER;
                }
                // otherwise add escape character and current character to field
                else {
                    csv_field.push_back(escape_character);
                    csv_field.push_back(c);
                }
                lastCharWasEscapeChar = false;
            }
            else {
                // escape character found
                if (c == escape_character) {
                    lastCharWasEscapeChar = true;
                }
                // enclose by character found
                else if (c == enclose_by_character) {
                    withinEnclosed = false;
                }
                // otherwise just add the char to the field
                else {
                    csv_field.push_back(c);
                }
            }
        }
        // not within enclosed
        else {
            // delimiter found
            if (c == delimiter) {
                return NEW_FIELD;
            }
            // endline found
            if (c == '\n') {
                // remove Windows line ending
                if (csv_field.size() && csv_field[csv_field.size() - 1] == '\r') {
                    csv_field.pop_back();
                }
                return NEW_LINE;
            }
            // enclose by character found
            if (c == enclose_by_character) {
                withinEnclosed = true;
            }
            else {
                csv_field.push_back(c);
            }
        }
        return OLD_FIELD;
    }

    /**
    * Helper function, that verifies a parsed csv line against the number of expected csv fields inferred from the first line of the csv file.
    * Once a csv line is verified it is added to the csv_fields_buffer.
    */
    void verifyAndAddParsedCsvLine(std::vector<std::string>& csv_fields) {
        if (csv_fields.size() != number_of_csv_columns && !ignore_malformed_csv) {
            std::string errorMsg = "csv input parse error: the csv input file's columns of: " + std::to_string(csv_fields.size()) + " doesn't match the expected column count of the first line of: " + std::to_string(number_of_csv_columns) + "\nvalues: ";
            if (csv_fields.size() > 0) {
                for (int i = 0; i < csv_fields.size(); i++) {
                    errorMsg.append(csv_fields[i] + ", ");
                }
                errorMsg = errorMsg.substr(0, errorMsg.size() - 2);
            }
            std::cerr << errorMsg;
            error = true;
            // add a dummy line to csv_fields_buffer to avoid deadlock
            csv_fields.clear();
            for (uint32_t i = 0; i < number_of_cs_table_columns; i++) {
                csv_fields.push_back("");
            }
            csv_fields_buffer->push(csv_fields);
        }
        else if (csv_fields.size() != number_of_csv_columns && ignore_malformed_csv) {
            ignored_malformed_csv_lines++;
            if (error_log) {
                std::string parsed_raw_csv_field_string = vectorToString(csv_fields);
                {
                    std::lock_guard<std::mutex> guard(m_errorFileStream);
                    this->errFileStream << "MALFORMED_CSV_LINE, -1, " << parsed_raw_csv_field_string << std::endl;
                }
            }
        }
        else {
            csv_fields_buffer->push(csv_fields);
        }
    }

    /**
    * Writes parsed csv fields to the specified ColumnStore table.
    */
    void writeCsvFieldsToColumnStoreTable(std::vector<std::string>& csv_fields) {
        mcsapi::columnstore_data_convert_status_t status;
        for (uint32_t col = 0; col < number_of_cs_table_columns; col++) {
            int32_t csvColumn = this->mapping[col];
            // set default values
            if (csvColumn == CUSTOM_DEFAULT_VALUE || csvColumn == COLUMNSTORE_DEFAULT_VALUE) {
                if ((customDefaultValue[col] == "" && this->tab.getColumn(col).isNullable()) || (this->nullOption == 1 && customDefaultValue[col] == "NULL" && this->tab.getColumn(col).isNullable())) {
                    bulk->setNull(col, &status);
                }
                else {
                    bulk->setColumn(col, customDefaultValue[col], &status);
                }
            }
            // set values from csv vector
            else {
                // if the vector contains an empty value insert it as NULL
                if ((csv_fields[csvColumn] == "") || (this->nullOption == 1 && csv_fields[csvColumn] == "NULL")) {
                    bulk->setNull(col, &status);
                }
                // if an (custom) input date format is specified and the target column is of type DATE or DATETIME, transform the input to ColumnStoreDateTime and inject it
                else if ((this->customInputDateFormat.find(col) != this->customInputDateFormat.end() || this->inputDateFormat != "") && (columnstore_column_types[col] == mcsapi::DATA_TYPE_DATE || columnstore_column_types[col] == mcsapi::DATA_TYPE_DATETIME)) {
                    if (this->customInputDateFormat.find(col) != this->customInputDateFormat.end()) {
                        mcsapi::ColumnStoreDateTime dt = mcsapi::ColumnStoreDateTime((std::string) csv_fields[csvColumn], this->customInputDateFormat[col]);
                        bulk->setColumn(col, dt, &status);
                    }
                    else {
                        mcsapi::ColumnStoreDateTime dt = mcsapi::ColumnStoreDateTime((std::string) csv_fields[csvColumn], this->inputDateFormat);
                        bulk->setColumn(col, dt, &status);
                    }
                }
                // if value is true and target column is any NUMERIC type, transform true to 1 and inject it
                else if (csv_fields[csvColumn] == "true" && (columnstore_column_types[col] == mcsapi::DATA_TYPE_BIGINT || columnstore_column_types[col] == mcsapi::DATA_TYPE_BIT || columnstore_column_types[col] == mcsapi::DATA_TYPE_DECIMAL || columnstore_column_types[col] == mcsapi::DATA_TYPE_DOUBLE || columnstore_column_types[col] == mcsapi::DATA_TYPE_FLOAT || columnstore_column_types[col] == mcsapi::DATA_TYPE_INT || columnstore_column_types[col] == mcsapi::DATA_TYPE_MEDINT || columnstore_column_types[col] == mcsapi::DATA_TYPE_SMALLINT || columnstore_column_types[col] == mcsapi::DATA_TYPE_TINYINT || columnstore_column_types[col] == mcsapi::DATA_TYPE_UBIGINT || columnstore_column_types[col] == mcsapi::DATA_TYPE_UDECIMAL || columnstore_column_types[col] == mcsapi::DATA_TYPE_UDOUBLE || columnstore_column_types[col] == mcsapi::DATA_TYPE_UFLOAT || columnstore_column_types[col] == mcsapi::DATA_TYPE_UINT || columnstore_column_types[col] == mcsapi::DATA_TYPE_UMEDINT || columnstore_column_types[col] == mcsapi::DATA_TYPE_USMALLINT || columnstore_column_types[col] == mcsapi::DATA_TYPE_UTINYINT)) {
                    bulk->setColumn(col, 1, &status);
                }
                else { // otherwise just inject the plain value as string
                    bulk->setColumn(col, (std::string) csv_fields[csvColumn], &status);
                }
            }
            if (error_log && status != mcsapi::CONVERT_STATUS_NONE) {
                //log the value and line that was saturated, invalid or truncated
                std::string statusValue;
                switch (status) {
                case mcsapi::CONVERT_STATUS_SATURATED:
                    statusValue = "SATURATED";
                    break;
                case mcsapi::CONVERT_STATUS_INVALID:
                    statusValue = "INVALID";
                    break;
                case mcsapi::CONVERT_STATUS_TRUNCATED:
                    statusValue = "TRUNCATED";
                    break;
                default:
                    statusValue = "UNKNOWN";
                }
                std::string parsed_raw_csv_field_string = vectorToString(csv_fields);
                {
                    std::lock_guard<std::mutex> guard(m_errorFileStream);
                    errFileStream << statusValue << ", " << csvColumn << ", " << parsed_raw_csv_field_string << std::endl;
                }
            }
        }
        bulk->writeRow();
    }

    /**
    * Generates an implicit 1:1 mapping of csv columns to cs columns.
    */
    void generateImplicitMapping(uint32_t csv_first_row_number_of_columns, bool default_non_mapped) {
        // check the column sizes of csv input and columnstore target for compatibility
        if (csv_first_row_number_of_columns < this->number_of_cs_table_columns && !default_non_mapped) {
            std::cerr << "Error: Column size of input file is less than the column size of the target table" << std::endl;
            clean();
            std::exit(2);
        }
        else if (csv_first_row_number_of_columns < this->number_of_cs_table_columns && default_non_mapped) {
            std::cout << "Warning: Column size of input file is less than the column size of the target table." << std::endl;
            std::cout << "Default values will be used for non mapped columnstore columns." << std::endl;
        }

        if (csv_first_row_number_of_columns > this->number_of_cs_table_columns) {
            std::cout << "Warning: Column size of input file is higher than the column size of the target table." << std::endl;
            std::cout << "Remaining csv columns won't be injected." << std::endl;
        }

        // generate the mapping
        for (uint32_t x = 0; x < this->number_of_cs_table_columns; x++) {
            if (x < csv_first_row_number_of_columns) {
                this->mapping[x] = x;
            }
            else { // map to the default value
                this->mapping[x] = this->COLUMNSTORE_DEFAULT_VALUE;
                this->customDefaultValue[x] = this->tab.getColumn(x).getDefaultValue();
            }
        }
    }

    /**
    * Generates an explicit mapping of cs to csv columns using the mapping file.
    */
    void generateExplicitMapping(int32_t csv_first_row_number_of_columns, bool default_non_mapped, std::string mapping_file) {

        // check if the mapping file exists
        std::ifstream map(mapping_file);
        if (!map) {
            std::cerr << "Error: Can't open mapping file " << mapping_file << std::endl;
            clean();
            std::exit(2);
        }
        map.close();

        // check if the yaml file is parseable
        YAML::Node yaml;
        try {
            yaml = YAML::LoadFile(mapping_file);
        }
        catch (YAML::ParserException& e) {
            std::cerr << "Error: Mapping file " << mapping_file << " couldn't be parsed." << std::endl << e.what() << std::endl;
            clean();
            std::exit(2);
        }

        // generate the mapping
        try {
            int32_t csv_column_counter = 0;
            for (std::size_t i = 0; i < yaml.size(); i++) {
                YAML::Node entry = yaml[i];
                // handling of the column definition expressions
                if (entry["column"]) {
                    int32_t csv_column = -1;
                    if (entry["column"].IsNull()) { // no explicit column number was given, use the implicit from csv_column_counter
                        csv_column = csv_column_counter;
                        csv_column_counter++;
                    }
                    else if (entry["column"].IsSequence()) { //ignore scalar
                        csv_column_counter++;
                    }
                    else if (entry["column"].IsDefined()) { // an explicit column number was given
                        csv_column = entry["column"].as<std::int32_t>();
                    }
                    // handle the mapping in non-ignore case
                    if (csv_column >= 0) {
                        // check if the specified csv column is valid
                        if (csv_column >= csv_first_row_number_of_columns) {
                            std::cerr << "Warning: Specified source column " << csv_column << " is out of bounds.  This mapping will be ignored." << std::endl;
                        }
                        // check if the specified target is valid
                        else if (!entry["target"]) {
                            std::cerr << "Warning: No target column specified for source column " << csv_column << ". This mapping will be ignored." << std::endl;
                        }
                        else if (getTargetId(entry["target"].as<std::string>()) < 0) {
                            std::cerr << "Warning: Specified target column " << entry["target"] << " could not be found. This mapping will be ignored." << std::endl;
                        } // if all tests pass, do the mapping
                        else {
                            int32_t targetId = getTargetId(entry["target"].as<std::string>());
                            if (this->mapping.find(targetId) != this->mapping.end()) {
                                std::cerr << "Warning: Already existing mapping for source column " << mapping[targetId] << " mapped to ColumnStore column " << targetId << " is overwritten by new mapping." << std::endl;
                            }
                            this->mapping[targetId] = csv_column;
                            handleOptionalColumnParameter(csv_column, targetId, entry);
                        }
                    }
                }
                // handling of the target definition expressions
                else if (entry["target"] && entry["target"].IsDefined()) { //target default value configuration
                    //check if the specified target is valid
                    if (getTargetId(entry["target"].as<std::string>()) < 0) {
                        std::cerr << "Warning: Specified target column " << entry["target"] << " could not be found. This target default value definition will be ignored." << std::endl;
                    }
                    // check if there is a default value defined
                    else if (!(entry["value"] && entry["value"].IsDefined())) {
                        std::cerr << "Warning: No default value specified for target column " << entry["target"] << ". This target default value definition will be ignored." << std::endl;
                    }
                    // if all tests pass, do the parsing
                    else {
                        std::int32_t targetId = getTargetId(entry["target"].as<std::string>());
                        if (this->mapping.find(targetId) != this->mapping.end()) {
                            std::cerr << "Warning: Already existing mapping for source column " << mapping[targetId] << " mapped to ColumnStore column " << targetId << " is overwritten by new default value." << std::endl;
                        }
                        if (entry["value"].as<std::string>() == "default") {
                            this->mapping[targetId] = COLUMNSTORE_DEFAULT_VALUE;
                            this->customDefaultValue[targetId] = this->tab.getColumn(targetId).getDefaultValue();
                        }
                        else {
                            this->mapping[targetId] = CUSTOM_DEFAULT_VALUE;
                            this->customDefaultValue[targetId] = entry["value"].as<std::string>();
                        }
                    }
                }
                else {
                    std::cerr << "Warning: Defined expression " << entry << " is not supported and will be ignored." << std::endl;
                }
            }
        }
        catch (std::exception& e) {
            std::cerr << "Error: Explicit mapping couldn't be generated. " << e.what() << std::endl;
            clean();
            std::exit(2);
        }

        // check if the mapping is valid and apply missing defaults if default_non_map was chosen
        for (uint32_t col = 0; col < this->number_of_cs_table_columns; col++) {
            if (this->mapping.find(col) == this->mapping.end()) {
                if (default_non_mapped) {
                    this->mapping[col] = COLUMNSTORE_DEFAULT_VALUE;
                    this->customDefaultValue[col] = this->tab.getColumn(col).getDefaultValue();
                    std::cout << "Notice: Using default value for ColumnStore column " << col << ": " << this->tab.getColumn(col).getColumnName() << std::endl;
                }
                else {
                    std::cerr << "Error: No mapping found for ColumnStore column " << col << ": " << this->tab.getColumn(col).getColumnName() << std::endl;
                    clean();
                    std::exit(2);
                }
            }
        }
    }

    /**
    * Helper function to handle optional column parameter like the custom input date format.
    */
    void handleOptionalColumnParameter(int32_t source, int32_t target, YAML::Node column) {
        // if there is already an old custom input date format entry delete it
        if (this->customInputDateFormat.find(target) != this->customInputDateFormat.end()) {
            this->customInputDateFormat.erase(target);
        }

        // set new custom input date format if applicable
        if (column["format"] && (this->tab.getColumn(target).getType() == mcsapi::DATA_TYPE_DATE || this->tab.getColumn(target).getType() == mcsapi::DATA_TYPE_DATETIME)) {
            //remove annotation marks from received custom date format
            std::string df = column["format"].as<std::string>();
            if (df[0] == '"' && df[df.size() - 1] == '"') {
                df = df.substr(1, df.size() - 1);
            }
            this->customInputDateFormat[target] = df;
        }
    }

    /**
    * Returns the target id of given string's columnstore representation if it can be found. otherwise -1.
    */
    int32_t getTargetId(std::string target) {
        try {
            int32_t targetId = std::stoi(target);
            this->tab.getColumn(targetId);
            return targetId;
        }
        catch (std::exception&) {}

        try {
            int32_t targetId = this->tab.getColumn(target).getPosition();
            return targetId;
        }
        catch (std::exception&) {}

        return -1;
    }

    /**
    * Concatenates a vector of strings to a single string
    */
    std::string vectorToString(std::vector <std::string> parsed_csv_fields) {
        std::string parsed_raw_csv_field_string;
        for (auto const& s : parsed_csv_fields) {
            parsed_raw_csv_field_string += s + ",";
        }
        if (parsed_raw_csv_field_string.size() > 0) {
            parsed_raw_csv_field_string = parsed_raw_csv_field_string.substr(0, parsed_raw_csv_field_string.size() - 1);
        }
        return parsed_raw_csv_field_string;
    }

    /**
    * Cleans the allocated memory.
    */
    void clean() {
        if (errFileStream.is_open()) {
            errFileStream.close();
        }
        if (csv_fields_buffer != nullptr) {
            delete(csv_fields_buffer);
        }
        if (file_input_buffer != nullptr) {
            delete(file_input_buffer);
        }
        if (bulk != nullptr) {
            delete(bulk);
        }
        if (driver != nullptr) {
            delete(driver);
        }
    }
};

/**
* Main function interacted with from command line.
* Responsible for parsing command line parameter, and starting the import process.
*/
int main(int argc, char* argv[])
{
    // Check if the command line arguments are valid
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " database table input_file [-m mapping_file] [-c Columnstore.xml] [-d delimiter] [-df date_format] [-n null_option] [-default_non_mapped] [-E enclose_by_character] [-C escape_character] [-rc read_cache] [-fib file_input_buffer] [-fibwt file_input_buffer_wait_time] [-clb csv_line_buffer] [-clbwt csv_line_buffer_wait_time] [-header] [-ignore_malformed_csv] [-err_log]" << std::endl;
        return 1;
    }

    // Parse the optional command line arguments
    InputParser input(argc, argv);
    std::string mappingFile;
    std::string columnStoreXML;
    std::string inputDateFormat;
    std::int32_t nullOption = 0;
    std::uint32_t read_cache_size = 1024 * 1024 * 2;
    std::uint32_t file_input_buffer_size = 1024 * 1024 * 200;
    std::uint32_t file_input_buffer_wait_time = 100;
    std::uint32_t csv_fields_buffer_size = 1500000;
    std::uint32_t csv_fields_buffer_wait_time = 100;
    bool default_non_mapped = false;
    bool ignore_malformed_csv = false;
    bool header = false;
    bool error_log = false;
    char delimiter = ',';
    char escape_character = '"';
    char enclose_by_character = '"';
    if (input.cmdOptionExists("-m")) {
        mappingFile = input.getCmdOption("-m");
    }
    if (input.cmdOptionExists("-c")) {
        columnStoreXML = input.getCmdOption("-c");
    }
    if (input.cmdOptionExists("-d")) {
        std::string delimiterString = input.getCmdOption("-d");
        if (delimiterString.length() != 1) {
            std::cerr << "Error: Delimiter needs to be one character. Current length: " << delimiterString.length() << std::endl;
            return 2;
        }
        delimiter = delimiterString[0];
    }
    if (input.cmdOptionExists("-n")) {
        try {
            nullOption = std::stoi(input.getCmdOption("-n"));
            if (nullOption < 0 || nullOption > 1) {
                std::cerr << "Error: The given null option parameter is out of range. Currently only values 0 [NULL string as data] and 1 [NULL string as NULL value] are supported." << std::endl;
                return 2;
            }
        }
        catch (std::exception&) {
            std::cerr << "Error: Couldn't parse null option parameter to an integer" << std::endl;
            return 2;
        }
    }
    if (input.cmdOptionExists("-C")) {
        std::string escapeString = input.getCmdOption("-C");
        if (escapeString.length() != 1) {
            std::cerr << "Error: Escape character needs to be one character. Current length: " << escapeString.length() << std::endl;
            return 2;
        }
        escape_character = escapeString[0];
    }
    if (input.cmdOptionExists("-E")) {
        std::string encloseByString = input.getCmdOption("-E");
        if (encloseByString.length() != 1) {
            std::cerr << "Error: Enclose by character needs to be one character. Current length: " << encloseByString.length() << std::endl;
            return 2;
        }
        enclose_by_character = encloseByString[0];
    }
    if (input.cmdOptionExists("-df")) {
        inputDateFormat = input.getCmdOption("-df");
    }
    if (input.cmdOptionExists("-default_non_mapped")) {
        default_non_mapped = true;
    }
    if (input.cmdOptionExists("-header")) {
        header = true;
    }
    if (input.cmdOptionExists("-ignore_malformed_csv")) {
        ignore_malformed_csv = true;
    }
    if (input.cmdOptionExists("-err_log")) {
        error_log = true;
    }

    if (input.cmdOptionExists("-fib")) {
        try {
            file_input_buffer_size = std::stoi(input.getCmdOption("-fib"));
            if (file_input_buffer_size < 33554432) {
                std::cerr << "Error: The given file input buffer parameter is out of range. A value higher than 33554431 needs to be inserted." << std::endl;
                return 2;
            }
        }
        catch (std::exception&) {
            std::cerr << "Error: Couldn't parse the file input buffer parameter to an unsigned integer" << std::endl;
            return 2;
        }
        //set the read_cache_size to 1% of the file input buffer size, if its more than 1MiB
        if (file_input_buffer_size / 100 > 1048576) {
            read_cache_size = file_input_buffer_size / 100;
        }
    }

    if (input.cmdOptionExists("-rc")) {
        try {
            read_cache_size = std::stoi(input.getCmdOption("-rc"));
            if (read_cache_size < 1048576) {
                std::cerr << "Error: The given read cache parameter is out of range. A value higher than 1048575 needs to be inserted." << std::endl;
                return 2;
            }
        }
        catch (std::exception&) {
            std::cerr << "Error: Couldn't parse the read cache parameter to an unsigned integer" << std::endl;
            return 2;
        }
    }

    if (input.cmdOptionExists("-fibwt")) {
        try {
            file_input_buffer_wait_time = std::stoi(input.getCmdOption("-fibwt"));
            if (file_input_buffer_wait_time < 10) {
                std::cerr << "Error: The given file input buffer wait time parameter is out of range. A value higher than 9 ms needs to be inserted." << std::endl;
                return 2;
            }
        }
        catch (std::exception&) {
            std::cerr << "Error: Couldn't parse the file input buffer wait time parameter to an unsigned integer" << std::endl;
            return 2;
        }
    }

    if (input.cmdOptionExists("-clb")) {
        try {
            csv_fields_buffer_size = std::stoi(input.getCmdOption("-clb"));
            if (csv_fields_buffer_size < 150000) {
                std::cerr << "Error: The given csv line buffer parameter is out of range. A value higher than 149999 needs to be inserted." << std::endl;
                return 2;
            }
        }
        catch (std::exception&) {
            std::cerr << "Error: Couldn't parse the csv line buffer parameter to an unsigned integer" << std::endl;
            return 2;
        }
    }

    if (input.cmdOptionExists("-clbwt")) {
        try {
            file_input_buffer_wait_time = std::stoi(input.getCmdOption("-clbwt"));
            if (file_input_buffer_wait_time < 10) {
                std::cerr << "Error: The given csv line buffer wait time parameter is out of range. A value higher than 9 ms needs to be inserted." << std::endl;
                return 2;
            }
        }
        catch (std::exception&) {
            std::cerr << "Error: Couldn't parse the csv line buffer wait time parameter to an unsigned integer" << std::endl;
            return 2;
        }
    }

    MCSRemoteImport* mcsimport = new MCSRemoteImport(argv[3], argv[1], argv[2], mappingFile, columnStoreXML, delimiter, inputDateFormat, default_non_mapped, escape_character, enclose_by_character, header, error_log, nullOption, ignore_malformed_csv, file_input_buffer_size, file_input_buffer_wait_time, csv_fields_buffer_size, csv_fields_buffer_wait_time, read_cache_size);
    int32_t rtn = mcsimport->import();
    return rtn;
}
