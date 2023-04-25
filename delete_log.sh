ips='172.17.0.2 172.17.0.3 172.17.0.4'
# ips='172.17.0.3 172.17.0.4'
for ip in $ips;do 
    echo -e  "\n\033[32m`date +'%Y/%m/%d %H:%M:%S'` $ip \033[0m"
    # ssh root@$ip "cd /opt/daos && rm -rf bin examples include  lib  lib64  prereq  share"
    # rsync -rucalpzv --exclude etc /opt/daos root@$ip:/opt/
    if [[ $ip == '172.17.0.2' ]];then
      rm -rf /tmp/daos*.log
      ls -alh /tmp/*.log
    else
      ssh $ip "rm -rf /tmp/daos*.log;ls -alh /tmp/*.log"
    fi
done

# ips1='172.17.0.2 172.17.0.3 172.17.0.4'
# function run_cmd(){
#         local command=$*
#         if [[ $* == "" ]]; then
#             echo "$1 cmd"
#         else
#                 for ip in $ips1; do
#                         echo -e  "\n\033[32m`date +'%Y/%m/%d %H:%M:%S'` $ip $*\033[0m"
#                         if [[ $ip == '172.17.0.2' ]];then
#                            eval ${command}
#                         else
#                            ssh $ip "${command}"
#                         fi
#                 done
#         fi
# }