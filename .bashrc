# .bashrc

# User specific aliases and functions

alias rm='rm -i'
alias cp='cp -i'
alias mv='mv -i'

# Source global definitions
if [ -f /etc/bashrc ]; then
	. /etc/bashrc
fi

export PATH=/opt/daos/bin:/home/daos/docker/daos/build/external/debug/spdk/scripts:$PATH

function daos_start(){
 daos_server start
}
function daos_format(){
  dmg storage format
}

ips1='172.17.0.2 172.17.0.3 172.17.0.4'
function run_cmd(){
	local command=$*
	if [[ $* == "" ]]; then
	    echo "$1 cmd"
	else
		for ip in $ips1; do
			echo -e  "\n\033[32m`date +'%Y/%m/%d %H:%M:%S'` $ip $*\033[0m"
                        if [[ $ip == '172.17.0.2' ]];then
                           eval ${command}
			else
			   ssh $ip "${command}"
			fi
		done
	fi
}

ips2='172.17.0.2 172.17.0.3 172.17.0.4'
function run_all(){
	local command=$*
	if [[ $* == "" ]]; then
	    echo "$1 cmd"
	else
		for ip in $ips2; do
			echo -e  "\n\033[32m`date +'%Y/%m/%d %H:%M:%S'` $ip $*\033[0m"
                        if [[ $ip == '172.17.0.2' ]];then
                           eval ${command}
			else
			    ssh $ip "${command}"
                        fi
		done
	fi
}
function greplog(){
    run_all "grep -rn '$1' /tmp/*.log"
}
function daos_conf(){
    cat /opt/daos/etc/daos_server.yml
}

daos_err(){
  dmg server set-logmasks err
}

daos_stop_all(){
  run_all 'pkill daos_agent;pkill daos_server'
}

daos_fuse(){
 dmg pool create sxb -z 4g
 daos cont create --oclass=RP_3GX --properties=rf:1 --type POSIX --pool sxb --label sxb
 mkdir -p /tmp/sxb
 export D_LOG_FILE=/tmp/daos_client.log
 export D_LOG_MASK=ERR 
 export DD_SUBSYS=all
 export DD_MASK=all
 dfuse --mountpoint=/tmp/sxb --pool=sxb --cont=sxb
}

daos_mount(){
 export D_LOG_FILE=/tmp/daos_client.log
 export D_LOG_MASK=ERR
 export DD_SUBSYS=all
 export DD_MASK=all
 dfuse --mountpoint=/tmp/sxb --pool=sxb --cont=sxb
}

gdb_dfuse(){
  gdb attach `ps aux|grep dfuse|grep -v grep|awk '{print$2}'`
}

daos_sys(){
  dmg sys query -v
}
daos_pool(){
 dms pool list -v
}
