# Monitoring Example Script
The script check_mariadbcs provides a simple example wrapper shell script over the mcsadmin utility that adheres to the nagios plugin specification. It can be invoked periodically by an appropriate monitoring agent to verify that the ColumnStore instance / cluster is fully active. Typically this should be run on one node, e.g. the primary node pm1.

