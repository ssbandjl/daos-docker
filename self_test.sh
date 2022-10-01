export D_LOG_MASK=debug
#export D_LOG_MASK=WARN
export DD_SUBSYS=all
#export DD_SUBSYS=mgmt,cart,mercury
export DD_MASK=all

export FI_LOG_LEVEL=debug
export HG_LOG_LEVEL=debug
# echo -e "self_test -u --group-name daos_server --endpoint 0:2 --message-size '(0 0)' --max-inflight-rpcs 16 --repetitions 10"
# self_test -u --group-name daos_server --endpoint 0:2 --message-size '(i1048576 b1048576)' --max-inflight-rpcs 16 --repetitions 100
self_test -u --group-name daos_server --master-endpoint 0:2 --endpoint 2:2 --message-size '(b1048576)' --max-inflight-rpcs 16 --repetitions 100
#self_test -u --group-name daos_server --master-endpoint 0:2 --endpoint 2:2 --message-size '(b1048576,b1048576)' --max-inflight-rpcs 16 --repetitions 100
