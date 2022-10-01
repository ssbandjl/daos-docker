set -e 

bash build.sh

#bash start.sh
bash rsync.sh

source /root/.bashrc
run_all "pkill daos_agent;pkill daos_server"
# run_all "cd /home/daos/docker/daos/;bash start.sh"
bash start.sh
