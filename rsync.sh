# ips='172.17.0.2 172.17.0.3 172.17.0.4'
ips='172.17.0.3 172.17.0.4'
for ip in $ips;do 
    echo -e  "\n\033[32m`date +'%Y/%m/%d %H:%M:%S'` $ip \033[0m"
    # ssh root@$ip "cd /opt/daos && rm -rf bin examples include  lib  lib64  prereq  share"
    # rsync -rucalpzv --exclude etc /opt/daos root@$ip:/opt/
    rsync -rucalpz --exclude etc /opt/daos root@$ip:/opt/
done
