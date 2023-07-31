#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <daos.h>

#define TEST_POOL_SIZE (1024*1024*1024)
#define POOL "pool"

void write_complete_cb(void *arg, int status)
{
    printf("Write completed with status %d\n", status);
    daos_event_complete((daos_event_t *)arg, status);
}

int main(int argc, char **argv)
{
    daos_handle_t poh, coh, oh;
    daos_obj_id_t oid;
    d_sg_list_t sgl;
    d_iov_t dkey;
    uuid_t uuid;
    daos_event_t event;
    daos_prop_t *prop = NULL;
    char str[37];
    int rc;
    d_iov_t iov;
    int      buf_len, tmp_len;
    daos_recx_t  recx[5];
    daos_iod_t   iod;

    /* 1. 初始化DAOS */
    rc = daos_init();
    if (rc != 0) {
        printf("Failed to initialize DAOS: %d\n", rc);
        exit(1);
    }

    /* 2. 连接到DAOS Pool */
    rc = daos_pool_connect(POOL, NULL, DAOS_PC_RW, &poh, NULL, NULL);
    if (rc != 0) {
        printf("Failed to connect to DAOS pool: %d\n", rc);
        exit(1);
    }
   


    /* 3. 创建DAOS Container */
    prop = daos_prop_alloc(1);
    assert_non_null(prop);
    prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_REDUN_FAC;
    prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_REDUN_RF0;
    rc = daos_cont_create(poh, &uuid, prop, NULL);
    if (rc != 0) {
        printf("Failed to create DAOS container: %d\n", rc);
        exit(1);
    }
    uuid_unparse(uuid, str);
    rc = daos_cont_open(poh, str, DAOS_COO_RW, &coh, NULL, NULL);
    if (rc != 0) {
        printf("Failed to open DAOS container: %d\n", rc);
        exit(1);
    }


    /* 4. 打开DAOS Object */
    memset(&oid, 0, sizeof(oid));
    oid.lo = 1;
    rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
    if (rc != 0) {
        printf("Failed to open DAOS object: %d\n", rc);
        exit(1);
    }

    /* 5. 准备写入数据 */
    char *data = "Hello, DAOS!";
    size_t data_len = strlen(data) + 1;
    sgl.sg_nr = 1;
    sgl.sg_nr_out = 0;
    d_iov_set(&iov, data, data_len);
    sgl.sg_iovs = &iov;

    /* 6. 发起异步写入操作 */
    daos_event_init(&event, poh, NULL);

    

    rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey,1, &iod, &sgl, &event);
    if (rc != 0) {
        printf("Failed to initiate write operation: %d\n", rc);
        exit(1);
    }

    /* 7. 等待异步操作完成 */
    daos_event_test(&event, 1, DAOS_EQ_WAIT);

    /* 8. 关闭DAOS Object */
    daos_obj_close(oh, NULL);

    /* 9. 销毁DAOS Container */
    daos_cont_destroy(poh, str, 0, NULL);

    /* 10. 断开DAOS Pool连接 */
    daos_pool_disconnect(poh, NULL);

    /* 11. 关闭DAOS */
    daos_fini();

    return 0;
}