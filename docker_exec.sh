docker ps |grep daos|grep 2.0.1
if [ $? != 0 ];then
  docker start daos
fi
docker exec -u root -it daos bash -c 'cd /home/daos/docker/daos/;exec "${SHELL:-sh}"'
# docker exec -u root -it daos2 bash -c 'cd /home/daos/docker/daos/;exec "${SHELL:-sh}"'
# docker exec -u root -it daos3 bash -c 'cd /home/daos/docker/daos/;exec "${SHELL:-sh}"'
