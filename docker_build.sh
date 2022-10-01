# rm -f cache_tgz
# echo "tar cache"
# tar -zcvf cache_tgz cache >/dev/null 2>&1

docker build  . -f utils/docker/Dockerfile.centos.7 -t daos201