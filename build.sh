set -e
#deps
#scons-3 --build-deps=yes --jobs 1 PREFIX=/opt/daos TARGET_TYPE=debug --deps-only

#daos
# scons-3 --jobs 1 install PREFIX=/opt/daos BUILD_TYPE=debug TARGET_TYPE=debug --build-deps=yes


# scons-3 --jobs 1 install PREFIX=/opt/daos BUILD_TYPE=debug TARGET_TYPE=debug
echo "scons-3 --jobs 1 install PREFIX=/opt/daos BUILD_TYPE=debug TARGET_TYPE=debug --build-deps=yes"
scons-3 --jobs 1 install PREFIX=/opt/daos BUILD_TYPE=debug TARGET_TYPE=debug --build-deps=yes
# scons-3 --jobs 1 install PREFIX=/opt/daos BUILD_TYPE=debug TARGET_TYPE=debug --config=force

echo -e "\nbuild ok"



# rsync -rucalpzv --exclude /Users/xb/OneDrive root@172.17.0.3:backup/Users/xb

# for ip in $ips;do rsync -rucalpzv --exclude etc /opt/daos root@$ip:/opt/;done
# for ip in $ips ;do rsync -rucalpzv /etc/daos/certs root@$ip:/etc/daos/;done
