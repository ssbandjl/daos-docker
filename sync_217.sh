ips="10.121.29.217"
for ip in $ips; do 
  rsync -rapvu /opt/daos root@$ip:/opt/
done
