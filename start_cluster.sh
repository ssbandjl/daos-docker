#!/bin/bash

docker start daos
docker exec -u root -it daos bash -c '/usr/sbin/sshd'
sleep 3
docker start daos2
docker exec -u root -it daos2 bash -c '/usr/sbin/sshd'
sleep 3
docker start daos3
docker exec -u root -it daos3 bash -c '/usr/sbin/sshd'

# for d in daos daos2 daos3;do
#   echo $d
#   docker exec -u root -it $d bash -c 'bash /home/daos/docker/daos/start.sh &'
# done

