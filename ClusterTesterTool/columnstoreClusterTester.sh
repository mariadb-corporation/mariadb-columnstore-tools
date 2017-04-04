#!/bin/bash

bold=$(tput bold)
normal=$(tput sgr0)


IPADDRESSES=""
OS="centos7"
PASSWORD="ssh"
USER=root
CHECK=true

OS_LIST=("centos6" "centos7" "debian8" "suse12" "ubuntu16")

NODE_IPADDRESS=""


checkContinue() {

  if [ "$CHECK" = false ]; then
    return 0
  fi
  
  echo ""
  read -p "Failure occurred, do you want to continue? (y,n) > " answer
  case ${answer:0:1} in
    y|Y )
      return 0
    ;;
    * ) 
      exit
    ;;
  esac
}

###
# Print Fucntions
###

helpPrint () {
          ################################################################################
    echo ""
    echo  "This is the MariaDB Columnstore Cluster System Test tool." 
    echo ""
    echo  "It will run a set of test to validate the setup of the MariaDB Columnstore system." 
    echo  "This can be run prior to the install to make sure the servers/nodes" 
    echo  "are configured properly. It should be run as the user of the planned install."
    echo  "Meaning if MariaDB Columnstore is going to be installed as root user, then run"
    echo  "this too from root user"
    echo ""
    echo "Additional information on Tool is documented at:"
    echo ""
    echo "https://mariadb.com/kb/en/mariadb/*****/"
    echo ""
    echo  "Items that are checked:" 
    echo  "	Node Ping test" 
    echo  "	Node SSH test" 
    echo  "	ColumnStore Port test"
    echo  "	OS version" 
    echo  "	Locale settings" 
    echo  "	Firewall settings" 
    echo  "	Dependent packages installed" 
    echo ""
    echo  "Usage: $0 [options]" 
    echo "OPTIONS:"
    echo  "   -h,--help			Help" 
    echo  "   --ipaddr=[ipaddresses]	Remote Node IP Addresses, if not provide, will only check local node" 
    echo  "   --os=[os]			Change OS Version (centos6, centos7, debian8, suse12, ubuntu16). (Default is centos7)" 
    echo  "   --password=[password]	Provide a user password. (Default: ssh-keys setup will be assumed)" 
    echo  "   -c,--continue		Continue on failures"
    echo ""
    echo  "NOTE: Dependent package : 'nmap' and 'expect' packages need to be installed locally" 
    echo ""
}

# Parse command line options.
while getopts hioupc:-: OPT; do
    case "$OPT" in
        h)
            echo $USAGE
            helpPrint
            exit 0
            ;;
        c)
            CHECK=false
            ;;      
        -)  LONG_OPTARG="${OPTARG#*=}"
            ## Parsing hack for the long style of arguments.
            case $OPTARG in
                help )  
                    helpPrint
                    exit 0
                    ;;            
		continue)
		    CHECK=false
		    ;;      
                ipaddr=?* )  
                    IPADDRESSES="$LONG_OPTARG"
                    ;;
                os=?* )
                    OS="$LONG_OPTARG"
                    ;;
#                user=?* )
#                    USER="$LONG_OPTARG"
#                    ;;
                password=?* )
                    PASSWORD="$LONG_OPTARG"
                    ;;
                ipaddr* )  
                    echo "No arg for --$OPTARG option" >&2
                    exit 1
                    ;;
#                user* )  
#                    echo "No arg for --$OPTARG option" >&2
#                    exit 1
#                    ;;
                os* )  
                    echo "No arg for --$OPTARG option" >&2
                    exit 1
                    ;;
                password* )  
                    echo "No arg for --$OPTARG option" >&2
                    exit 1
                    ;;
                help* )  
                    helpPrint
                    exit 0
                    ;;                                        
                 '' )
                    break ;; # "--" terminates argument processing
                * )
                    echo "Illegal option --$OPTARG" >&2
                    exit 1
                    ;;
            esac 
            ;;       
        \?)
            # getopts issues an error message
            echo $USAGE >&2
            exit 1
            ;;
    esac
done

# Remove the switches we parsed above.
shift `expr $OPTIND - 1`

match=false
for SUPPORTED_OS in "${OS_LIST[@]}"; do
  if [ "$OS" = "$SUPPORTED_OS" ] ; then
    match=true;
    break
  fi
done

if ! $match ; then
  echo ""
  echo "$OS is not supported, enter one of the following OS's"
  echo ""
  for SUPPORTED_OS in "${OS_LIST[@]}"; do
    echo "$SUPPORTED_OS"
  done
  echo ""
  exit 1
fi

if [ "$IPADDRESSES" != "" ]; then
  #parse IP Addresses into an array
  IFS=','
  read -ra NODE_IPADDRESS <<< "$IPADDRESSES"
  
  if ! type expect > /dev/null 2>&1 ; then
      echo "expect is not installed. Please install and rerun."
      exit 1
  fi

  if ! type nmap > /dev/null 2>&1; then
      echo "nmap is not installed. Please install and rerun."
      exit 1
  fi
fi

echo ""  
echo "*** This is the MariaDB Columnstore Cluster System test tool ***"
echo ""

# run the remote node valdiation test
if [ "$IPADDRESSES" != "" ]; then
  # ping test
  #
  echo "Run Ping access Test to remote nodes"
  echo ""

  pass=true
  for ipadd in "${NODE_IPADDRESS[@]}"; do

    `ping $ipadd -c 1 -w 5 > /dev/null`
    if [ "$?" -eq 0 ]; then
      echo $ipadd " Passed ping test"
    else
      echo $ipadd " ${bold}Failed${normal} ping test"
      pass=false
    fi
  done

  if ! $pass; then
    checkContinue
  fi

  # Login test
  #
  echo ""
  echo "Run SSH Login access Test to remote nodes"
  echo ""

  pass=true
  for ipadd in "${NODE_IPADDRESS[@]}"; do
    `./remote_command.sh $ipadd $PASSWORD ls 1 > /tmp/remote_command 2>&1`;
    if [ "$?" -eq 0 ]; then
      echo $ipadd " Passed SSH login test"
    else
      echo $ipadd " ${bold}Failed${normal} SSH login test, check password or ssh-key settings"
      pass=false
    fi
  done

  if ! $pass; then
    checkContinue
  fi

  # Os check
  #
  echo ""
  echo "Run OS check - OS version needs to be the same on all nodes"
  echo ""
  
  #get local OS
  `./os_check.sh > /tmp/os_check 2>&1`
  if [ "$?" -eq 0 ]; then
    echo "Local OS Version : `cat /tmp/os_check`"
  else
    echo "Error running os_check.sh on local node"
  fi
  
  pass=true
  for ipadd in "${NODE_IPADDRESS[@]}"; do
    `./remote_scp_put.sh $ipadd $PASSWORD os_check.sh 1 > /tmp/remote_scp_put_check 2>&1`
    if [ "$?" -ne 0 ]; then
      echo "Error running remote_scp_put.sh to $ipadd node, check /tmp/remote_scp_put_check"
    else
      `./remote_command.sh $ipadd $PASSWORD './os_check.sh > /tmp/os_check 2>&1' > /dev/null`
      if [ "$?" -eq 0 ]; then
	`./remote_scp_get.sh $ipadd $PASSWORD /tmp/os_check > /tmp/remote_scp_get_check 2>&1`
	if [ "$?" -ne 0 ]; then
	  echo "Error running remote_scp_get.sh to $ipadd node, check /tmp/remote_scp_get_check"
	else
	  echo "$ipadd OS Version : `cat os_check`"
	  `diff /tmp/os_check os_check > /dev/null 2>&1`
	  if [ "$?" -ne 0 ]; then
	    echo "${bold}Failed${normal}, $ipadd has a different OS than local node"
	    pass=false
	  fi
	  `rm -f os_check`
	fi
      else
	echo "Error running os_check.sh on $ipadd node"
	pass=false
      fi
    fi
  done

  if ! $pass; then
    checkContinue
  fi

  # Locale check
  #
  echo ""
  echo "Run Locale check - Locale needs to be the same on all nodes"
  echo ""
  
  #get local Locale
  `locale | grep LANG= > /tmp/locale_check 2>&1`
  if [ "$?" -eq 0 ]; then
    echo "Local Locale : `cat /tmp/locale_check`"
  else
    echo "Error running 'locale' command on local node"
  fi
  
  pass=true
  for ipadd in "${NODE_IPADDRESS[@]}"; do
    `./remote_command.sh $ipadd $PASSWORD 'locale | grep LANG= > /tmp/locale_check 2>&1' > /dev/null`
    if [ "$?" -eq 0 ]; then
      `./remote_scp_get.sh $ipadd $PASSWORD /tmp/locale_check > /tmp/remote_scp_get_check 2>&1`
      if [ "$?" -ne 0 ]; then
	echo "Error running remote_scp_get.sh to $ipadd node, check /tmp/remote_scp_get_check"
      else
	echo "$ipadd Locale : `cat locale_check`"
	`diff /tmp/locale_check locale_check > /dev/null 2>&1`
	if [ "$?" -ne 0 ]; then
	  echo "${bold}Failed${normal}, $ipadd has a different Locale setting than local node"
	  pass=false
	fi
	`rm -f locale_check`
      fi
    fi
  done
  
  if ! $pass; then
    checkContinue
  fi

  # SELINUX check
  #
  echo ""
  echo "Run SELINUX check - Setting should to be disabled on all nodes"
  echo ""
  
  pass=true
  #check local SELINUX
  if [ -f /etc/selinux/config ]; then
    `cat /etc/selinux/config | grep SELINUX | grep enforcing > /tmp/selinux_check 2>&1`
    if [ "$?" -eq 0 ]; then
      echo "${bold}Failed${normal}, Local SELINUX setting is Enabled, please disable"
      pass=false
    else
      echo "Local SELINUX setting is Not Enabled"
    fi
  else
      echo "Local SELINUX setting is Not Enabled"
  fi
  
  for ipadd in "${NODE_IPADDRESS[@]}"; do
    `./remote_scp_get.sh $ipadd $PASSWORD /etc/selinux/config > /tmp/remote_scp_get_check 2>&1`
    if [ "$?" -ne 0 ]; then
      echo "$ipadd SELINUX setting is Not Enabled"
    else
     `cat config | grep SELINUX | grep enforcing > /tmp/selinux_check 2>&1`
    if [ "$?" -eq 0 ]; then
      echo "${bold}Failed${normal}, $ipadd SELINUX setting is Enabled, please disable"
      pass=false
    else
      echo "$ipadd SELINUX setting is Not Enabled"
    fi
      `rm -f config`
    fi
  done

  if ! $pass; then
    checkContinue
  fi

  # IPTABLES check
  #
  echo ""
  echo "Run IPTABLES check - Service should to be disabled on all nodes"
  echo ""
  
  pass=true
  #check local IPTABLES
  `chkconfig > /tmp/iptables_check 2>&1`
  `cat /tmp/iptables_check | grep iptables | grep on > /dev/null 2>&1`
  if [ "$?" -eq 0 ]; then
    echo "${bold}Failed${normal}, Local IPTABLES service is Enabled, please disable"
    pass=false
  else
    echo "Local IPTABLES service is Not Enabled"
  fi

  for ipadd in "${NODE_IPADDRESS[@]}"; do
    `./remote_command.sh $ipadd $PASSWORD 'chkconfig > /tmp/iptables_check 2>&1' > /dev/null`
    if [ "$?" -eq 0 ]; then
      `./remote_scp_get.sh $ipadd $PASSWORD /tmp/iptables_check > /tmp/remote_scp_get_check 2>&1`
      if [ "$?" -ne 0 ]; then
	echo "Error running remote_scp_get.sh to $ipadd node, check /tmp/remote_scp_get_check"
      else
        `cat iptables_check | grep iptables | grep on > /dev/null 2>&1`
	if [ "$?" -eq 0 ]; then
	  echo "${bold}Failed${normal}, $ipadd IPTABLES service is Enabled, please disable"
	  pass=false
	else
	  echo "$ipadd IPTABLES service is Not Enabled"
	fi
	`rm -f iptables_check`
      fi
    fi
  done

  if ! $pass; then
    checkContinue
  fi

  # UFW check
  #
  echo ""
  echo "Run UFW check - Service should to be disabled on all nodes"
  echo ""
  
  pass=true
  #check local UFW
  `chkconfig > /tmp/ufw_check 2>&1`
  `cat /tmp/ufw_check | grep ufw | grep on > /dev/null 2>&1`
  if [ "$?" -eq 0 ]; then
    echo "${bold}Failed${normal}, Local UFW service is Enabled, please disable"
    pass=false
  else
    echo "Local UFW service is Not Enabled"
  fi

  for ipadd in "${NODE_IPADDRESS[@]}"; do
    `./remote_command.sh $ipadd $PASSWORD 'chkconfig > /tmp/ufw_check 2>&1' > /dev/null`
    if [ "$?" -eq 0 ]; then
      `./remote_scp_get.sh $ipadd $PASSWORD /tmp/ufw_check > /tmp/remote_scp_get_check 2>&1`
      if [ "$?" -ne 0 ]; then
	echo "Error running remote_scp_get.sh to $ipadd node, check /tmp/remote_scp_get_check"
      else
        `cat ufw_check | grep ufw | grep on > /dev/null 2>&1`
	if [ "$?" -eq 0 ]; then
	  echo "${bold}Failed${normal}, $ipadd UFW service is Enabled, please disable"
	  pass=false
	else
	  echo "$ipadd UFW service is Not Enabled"
	fi
	`rm -f ufw_check`
      fi
    fi
  done

  if ! $pass; then
    checkContinue
  fi

  # rcSuSEfirewall2 check
  #
  echo ""
  echo "Run rcSuSEfirewall2 check - Service should to be disabled on all nodes"
  echo ""
  
  pass=true
  #check local IPTABLES
  `/sbin/rcSuSEfirewall2 status > /tmp/rcSuSEfirewall2_check 2>&1`
  `cat /tmp/rcSuSEfirewall2_check | grep active > /dev/null 2>&1`
  if [ "$?" -eq 0 ]; then
    echo "${bold}Failed${normal}, Local rcSuSEfirewall2 service is Enabled, please disable"
    pass=false
  else
    echo "Local rcSuSEfirewall2 service is Not Enabled"
  fi

  for ipadd in "${NODE_IPADDRESS[@]}"; do
    `./remote_command.sh $ipadd $PASSWORD '/sbin/rcSuSEfirewall2 status > /tmp/rcSuSEfirewall2_check 2>&1' > /dev/null`
    if [ "$?" -eq 0 ]; then
      `./remote_scp_get.sh $ipadd $PASSWORD /tmp/rcSuSEfirewall2_check > /tmp/remote_scp_get_check 2>&1`
      if [ "$?" -ne 0 ]; then
	echo "Error running remote_scp_get.sh to $ipadd node, check /tmp/remote_scp_get_check"
      else
        `cat rcSuSEfirewall2_check | grep active > /dev/null 2>&1`
	if [ "$?" -eq 0 ]; then
	  echo "${bold}Failed${normal}, $ipadd rcSuSEfirewall2 service is Enabled, please disable"
	  pass=false
	else
	  echo "$ipadd rcSuSEfirewall2 service is Not Enabled"
	fi
	`rm -f rcSuSEfirewall2_check`
      fi
    fi
  done

  if ! $pass; then
    checkContinue
  fi

  # firewalld check
  #
  echo ""
  echo "Run FIREWALLD check - Service should to be disabled on all nodes"
  echo ""
  
  pass=true
  #check local FIREWALLD
  `systemctl status firewalld > /tmp/firewalld_check 2>&1`
  `cat /tmp/firewalld_check | grep running > /dev/null 2>&1`
  if [ "$?" -eq 0 ]; then
    echo "${bold}Failed${normal}, Local FIREWALLD service is Enabled, please disable"
    pass=false
  else
    echo "Local FIREWALLD service is Not Enabled"
  fi

  for ipadd in "${NODE_IPADDRESS[@]}"; do
    `./remote_command.sh $ipadd $PASSWORD 'systemctl status firewalld > /tmp/firewalld_check 2>&1' > /dev/null`
    if [ "$?" -eq 0 ]; then
      `./remote_scp_get.sh $ipadd $PASSWORD /tmp/firewalld_check > /tmp/remote_scp_get_check 2>&1`
      if [ "$?" -ne 0 ]; then
	echo "Error running remote_scp_get.sh to $ipadd node, check /tmp/remote_scp_get_check"
      else
        `cat firewalld_check | grep running > /dev/null 2>&1`
	if [ "$?" -eq 0 ]; then
	  echo "${bold}Failed${normal}, $ipadd FIREWALLD service is Enabled, please disable"
	  pass=false
	else
	  echo "$ipadd FIREWALLD service is Not Enabled"
	fi
	`rm -f firewalld_check`
      fi
    fi
  done

  if ! $pass; then
    checkContinue
  fi

  # port test
  #
  echo ""
  echo "Run MariaDB ColumnStore Port (8602) availibility test"
  echo ""

  pass=true
  for ipadd in "${NODE_IPADDRESS[@]}"; do

    `nmap $ipadd -p 8602 | grep 'closed unknown' > /dev/null`
    if [ "$?" -eq 0 ]; then
      echo $ipadd " Passed port test"
    else
      echo $ipadd " ${bold}Failed${normal} port test"
      pass=false
    fi
  done

  if ! $pass; then
    checkContinue
  fi

fi

#
# now check packaging on local and remote nodes
#

echo ""
echo "Run MariaDB ColumnStore Dependent Package Check"
echo ""

declare -a CENTOS_PKG=("boost" "expect" "perl" "perl-DBI" "openssl" "zlib" "file" "sudo" "perl-DBD-MySQL" "libaio" "rsync" "snappy" "net-tools")

if [ $OS == "centos6" ] || [ $OS == "centos7" ]; then
  pass=true
  #check centos packages on local node
  for PKG in "${CENTOS_PKG[@]}"; do
    `yum list installed "$PKG" > /tmp/pkg_check 2>&1`
    `cat /tmp/pkg_check | grep 'command not found' > /dev/null 2>&1`
    if [ "$?" -eq 0 ]; then
      echo "${bold}Failed${normal}, Local node ${bold}yum${normal} package not installed"
      pass=false
      break
    else
      `cat /tmp/pkg_check | grep Installed > /dev/null 2>&1`
      if [ "$?" -ne 0 ]; then
	echo "${bold}Failed${normal}, Local node package ${bold}${PKG}${normal} is not installed, please install"
	pass=false
      fi
    fi
  done

  if $pass; then
    echo "Local node - Passed, all dependency packages are installed"
  else
    checkContinue
  fi

  pass=true
  if [ "$IPADDRESSES" != "" ]; then
    for ipadd in "${NODE_IPADDRESS[@]}"; do
      for PKG in "${CENTOS_PKG[@]}"; do
	`./remote_command.sh $ipadd $PASSWORD "yum list installed '$PKG' > /tmp/pkg_check 2>&1" 1 > /tmp/remote_command 2>&1`
	if [ "$?" -eq 0 ]; then
	  `./remote_scp_get.sh $ipadd $PASSWORD /tmp/pkg_check > /tmp/remote_scp_get_check 2>&1`
	    if [ "$?" -ne 0 ]; then
	      echo "Error running remote_scp_get.sh to $ipadd node, check /tmp/remote_scp_get_check"
	    else
	      `cat pkg_check | grep 'command not found' > /dev/null 2>&1`
	      if [ "$?" -eq 0 ]; then
		echo "${bold}Failed${normal}, $ipadd node ${bold}yum${normal} package not installed"
		pass=false
		break
	      else
		`cat pkg_check | grep Installed > /dev/null 2>&1`
		if [ "$?" -ne 0 ]; then
		  echo "${bold}Failed${normal}, $ipadd package ${bold}${PKG}${normal} is not installed, please install"
		  pass=false
		fi
	      `rm -f pkg_check`
	    fi
	  fi
	fi
      done
      
      if $pass; then
	echo "$ipadd node - Passed, all dependency packages are installed"
      else
	checkContinue
      fi
    done
  fi
fi

if [ $OS == "suse12" ]; then
  pass=true
  #check centos packages on local node
  for PKG in "${CENTOS_PKG[@]}"; do
    `zypper list installed "$PKG" > /tmp/pkg_check 2>&1`
    `cat /tmp/pkg_check | grep 'command not found' > /dev/null 2>&1`
    if [ "$?" -eq 0 ]; then
      echo "${bold}Failed${normal}, Local node ${bold}zypper${normal} package not installed"
      pass=false
      break
    else
      `cat /tmp/pkg_check | grep Installed > /dev/null 2>&1`
      if [ "$?" -ne 0 ]; then
	echo "${bold}Failed${normal}, Local node package ${bold}${PKG}${normal} is not installed, please install"
	pass=false
      fi
    fi
  done

  if $pass; then
    echo "Local node - Passed, all dependency packages are installed"
  else
    checkContinue
  fi

  pass=true
  if [ "$IPADDRESSES" != "" ]; then
    for ipadd in "${NODE_IPADDRESS[@]}"; do
      for PKG in "${CENTOS_PKG[@]}"; do
	`./remote_command.sh $ipadd $PASSWORD "zypper list installed 1$PKG1 > /tmp/pkg_check 2>&1" 1 > /tmp/remote_command 2>&1`
	if [ "$?" -eq 0 ]; then
	  `./remote_scp_get.sh $ipadd $PASSWORD /tmp/pkg_check > /tmp/remote_scp_get_check 2>&1`
	    if [ "$?" -ne 0 ]; then
	      echo "Error running remote_scp_get.sh to $ipadd node, check /tmp/remote_scp_get_check"
	    else
	      `cat /tmp/remote_command | grep 'command not found' > /dev/null 2>&1`
	      if [ "$?" -eq 0 ]; then
		echo "${bold}Failed${normal}, $ipadd ${bold}zypper${normal} package not installed"
		pass=false
		break
	      else
		`cat pkg_check | grep Installed > /dev/null 2>&1`
		if [ "$?" -ne 0 ]; then
		  echo "${bold}Failed${normal}, $ipadd package ${bold}${PKG}${normal} is not installed, please install"
		  pass=false
		fi
	      `rm -f pkg_check`
	    fi
	  fi
	fi
      done
      
      if $pass; then
	echo "$ipadd node - Passed, all dependency packages are installed"
      else
	checkContinue
      fi
    done
  fi
fi  

declare -a DEBIAN_PKG=("libboost-all-dev" "expect" "libdbi-perl" "perl" "openssl" "libreadline-dev" "rsync" "snappy" "net-tools")
	
if [ $OS == "ubuntu16" ] || [ $OS == "debian8" ]; then
  pass=true
  #check centos packages on local node
  for PKG in "${DEBIAN_PKG[@]}"; do
    `dpkg -s "$PKG" > /tmp/pkg_check 2>&1`
    `cat /tmp/pkg_check | grep 'command not found' > /dev/null 2>&1`
    if [ "$?" -eq 0 ]; then
      echo "${bold}Failed${normal}, Local node ${bold}dpkg${normal} package not installed"
      pass=false
      break
    else
      `cat /tmp/pkg_check | grep 'install ok' > /dev/null 2>&1`
      if [ "$?" -ne 0 ]; then
	echo "${bold}Failed${normal}, Local node package ${bold}${PKG}${normal} is not installed, please install"
	pass=false
      fi
    fi
  done

  if $pass; then
    echo "Local node - Passed, all dependency packages are installed"
  else
    checkContinue
  fi

  pass=true
  if [ "$IPADDRESSES" != "" ]; then
    for ipadd in "${NODE_IPADDRESS[@]}"; do
      for PKG in "${DEBIAN_PKG[@]}"; do
	`./remote_command.sh $ipadd $PASSWORD "dpkg -s '$PKG' > /tmp/pkg_check 2>&1" 1 > /tmp/remote_command 2>&1`
	if [ "$?" -eq 0 ]; then
	  `./remote_scp_get.sh $ipadd $PASSWORD /tmp/pkg_check > /tmp/remote_scp_get_check 2>&1`
	    if [ "$?" -ne 0 ]; then
	      echo "Error running remote_scp_get.sh to $ipadd node, check /tmp/remote_scp_get_check"
	    else
	      `cat /tmp/remote_command | grep 'command not found' > /dev/null 2>&1`
	      if [ "$?" -eq 0 ]; then
		echo "${bold}Failed${normal}, $ipadd ${bold}dpkg${normal} package not installed"
		pass=false
		break
	      else
		`cat pkg_check | grep 'install ok' > /dev/null 2>&1`
		if [ "$?" -ne 0 ]; then
		  echo "${bold}Failed${normal}, $ipadd package ${bold}${PKG}${normal} is not installed, please install"
		  pass=false
		fi

		`rm -f pkg_check`
	      fi
	    fi
	fi
      done
      
      if $pass; then
	echo "$ipadd node - Passed, all dependency packages are installed"
      else
	checkContinue
      fi
    done
  fi
fi

echo ""
echo ""
echo "Finished Validation of the Cluster, correct any failures"
echo ""
echo ""

exit 0
