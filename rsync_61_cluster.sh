ips="182.200.53.61 182.200.53.62"
for ip in $ips; do 
  rsync -rapvu /opt/daos root@$ip:/opt/
done
