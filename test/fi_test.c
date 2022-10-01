/**********************************************************************
 * 	Simple Hello Test
 * 	for
 * 	Open Fabric Interface 1.x
 *
 * 	Jianxin Xiong
 * 	(jianxin.xiong@intel.com)
 * 	2013-2017
 * ********************************************************************/

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <rdma/fabric.h>
#include <rdma/fi_atomic.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_tagged.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHK_ERR(name, cond, err)                                               \
  do {                                                                         \
    if (cond) {                                                                \
      fprintf(stderr, "%s: %s\n", name, strerror(-(err)));                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static struct fi_info *fi;
static struct fid_fabric *fabric;
static struct fid_domain *domain;
static struct fid_av *av;
static struct fid_ep *ep;
static struct fid_cq *cq;
static fi_addr_t peer_addr;
static struct fi_context sctxt;
static struct fi_context rctxt;
static char sbuf[64];
static char rbuf[64];

static void get_peer_addr(void *peer_name) {
  int err;
  char buf[64];
  size_t len = 64;

  buf[0] = '\0';
  fi_av_straddr(av, peer_name, buf, &len);
  printf("Translating peer address: %s\n", buf);

  err = fi_av_insert(av, peer_name, 1, &peer_addr, 0, NULL);
  CHK_ERR("fi_av_insert", (err != 1), err);
}

static void init_fabric(char *server) {
  struct fi_info *hints;
  struct fi_cq_attr cq_attr;
  struct fi_av_attr av_attr;
  int err;
  int version;
  char name[64], buf[64];
  size_t len = 64;
  // 提示
  hints = fi_allocinfo();
  CHK_ERR("fi_allocinfo", (!hints), -ENOMEM);

  memset(&cq_attr, 0, sizeof(cq_attr));
  memset(&av_attr, 0, sizeof(av_attr));
  /*
  注意：如果没有ULL/UL/L后缀，则系统默认为 int类型.
  1ULL:表示1是unsigned long long 类型(64位系统占8byte，64位)
  1UL:表示1是unsigned long 类型(64位系统占8byte，64位)
  1L:表示1是long 类型(64位系统占8byte，64位)
  */
  hints->ep_attr->type = FI_EP_RDM; // 可靠数据报 (FI_EP_RDM)
  hints->caps = FI_MSG; // 1ULL=Unsigned Long long 无符号长整型
  hints->mode = FI_CONTEXT;
  // hints->fabric_attr->prov_name = strdup("psm2");
  hints->fabric_attr->prov_name = strdup("sockets");
  // hints->fabric_attr->prov_name = strdup("shm");
  /*
  static void ofi_ordered_provs_init(void)
// {
// 	char *ordered_prov_names[] = {
// 		"efa", "psm2", "psm", "usnic", "gni", "bgq", "verbs",
// 		"netdir", "psm3", "ofi_rxm", "ofi_rxd", "shm",
// 		/* Initialize the socket based providers last of the
// 		 * standard providers.  This will result in them being
// 		 * the least preferred providers.
// 		 */

  // 		/* Before you add ANYTHING here, read the comment above!!! */
  // 		"udp", "tcp", "sockets", /* NOTHING GOES HERE! */
  // 		/* Seriously, read it! */

  // 		/* These are hooking providers only.  Their order
  // 		 * doesn't matter
  // 		 */
  // 		"ofi_hook_perf", "ofi_hook_debug", "ofi_hook_noop",
  // 	};

  version = FI_VERSION(1, 0);
  err =
      fi_getinfo(version, server, "12345", server ? 0 : FI_SOURCE, hints, &fi);
  CHK_ERR("fi_getinfo:", (err < 0), err);

  fi_freeinfo(hints);

  printf("Using OFI device: %s\n", fi->fabric_attr->name);

  err = fi_fabric(fi->fabric_attr, &fabric, NULL);
  CHK_ERR("fi_fabric", (err < 0), err);

  err = fi_domain(fabric, fi, &domain, NULL);
  CHK_ERR("fi_domain", (err < 0), err);

  av_attr.type = FI_AV_UNSPEC;

  err = fi_av_open(domain, &av_attr, &av, NULL);
  CHK_ERR("fi_av_open", (err < 0), err);

  cq_attr.format = FI_CQ_FORMAT_TAGGED;
  cq_attr.size = 100;

  err = fi_cq_open(domain, &cq_attr, &cq, NULL);
  CHK_ERR("fi_cq_open", (err < 0), err);

  err = fi_endpoint(domain, fi, &ep, NULL);
  CHK_ERR("fi_endpoint", (err < 0), err);

  err = fi_ep_bind(ep, (fid_t)cq, FI_SEND | FI_RECV);
  CHK_ERR("fi_ep_bind cq", (err < 0), err);

  err = fi_ep_bind(ep, (fid_t)av, 0);
  CHK_ERR("fi_ep_bind av", (err < 0), err);
  // fi_enable Transitions an active endpoint into an enabled state
  err = fi_enable(ep);
  CHK_ERR("fi_enable", (err < 0), err);
  /*
  fi_getname / fi_getpeer
  fi_getname 和 fi_getpeer 调用可分别用于检索本地或对等端点地址。
  在输入时，addrlen 参数应指示 addr 缓冲区的大小。
  如果实际地址大于缓冲区可以容纳的地址，它将被截断并返回 -FI_ETOOSMALL。
  在输出时，addrlen 设置为存储地址所需的缓冲区大小，可能大于输入值。
  在启用指定端点或分配地址之前，fi_getname 不能保证返回有效的源地址。
  端点可以通过 fi_enable 显式启用，也可以隐式启用，例如通过 fi_connect 或
  fi_listen。 可以使用 fi_setname 分配地址。 在端点完全连接之前，fi_getpeer
  不能保证返回有效的对等地址——已生成 FI_CONNECTED 事件。
  */
  err = fi_getname((fid_t)ep, name, &len);
  CHK_ERR("fi_getname", (err < 0), err);

  buf[0] = '\0';
  len = 64;
  // Convert an address into a printable string
  fi_av_straddr(av, name, buf, &len);
  printf("My address is %s\n", buf);

  if (server)
    get_peer_addr(fi->dest_addr);
}

static finalize_fabric(void) {
  fi_close((fid_t)ep);
  fi_close((fid_t)cq);
  fi_close((fid_t)av);
  fi_close((fid_t)domain);
  fi_close((fid_t)fabric);
  fi_freeinfo(fi);
}

static void wait_cq(void) {
  struct fi_cq_err_entry entry;
  int ret, completed = 0;
  fi_addr_t from;

  while (!completed) {
    ret = fi_cq_readfrom(cq, &entry, 1, &from);
    if (ret == -FI_EAGAIN)
      continue;

    if (ret == -FI_EAVAIL) {
      ret = fi_cq_readerr(cq, &entry, 1);
      CHK_ERR("fi_cq_readerr", (ret != 1), ret);

      printf("Completion with error: %d\n", entry.err);
      if (entry.err == FI_EADDRNOTAVAIL)
        get_peer_addr(entry.err_data);
    }

    CHK_ERR("fi_cq_read", (ret < 0), ret);
    completed += ret;
  }
}

static void send_one(int size) {
  int err;

  CHK_ERR("send_one", (peer_addr == 0ULL), -EDESTADDRREQ);

  err = fi_send(ep, sbuf, size, NULL, peer_addr, &sctxt);
  CHK_ERR("fi_send", (err < 0), err);

  wait_cq();
}

static void recv_one(int size) {
  int err;

  err = fi_recv(ep, rbuf, size, NULL, FI_ADDR_UNSPEC, &rctxt);
  CHK_ERR("fi_recv", (err < 0), err);

  wait_cq();
}
//gcc fi_test.c -o fi_test -I/home/daos/docker/daos/build/external/debug/ofi/include/ -g -L/opt/daos/prereq/debug/ofi/lib/ -Xlinker -R/opt/daos/prereq/debug/ofi/lib/ -lfabric
int main(int argc, char *argv[]) {
  int is_client = 0;
  char *server = NULL;
  int size = 64;

  if (argc > 1) {
    is_client = 1;
    server = argv[1];
    strcpy(sbuf, "Hello server, I am client.");
  } else {
    strcpy(sbuf, "Hello client, I am server.");
  }
  printf("server:%s\n", server);
  init_fabric(server);

  if (is_client) {
    printf("Sending '%s' to server\n", sbuf);
    send_one(size);
    recv_one(size);
    printf("Received '%s' from server\n", rbuf);
  } else {
    while (1) {
      printf("Waiting for client\n");
      recv_one(size);
      printf("Received '%s' from client\n", rbuf);
      printf("Sending '%s' to client\n", sbuf);
      send_one(size);
      printf("Done, press RETURN to continue, 'q' to exit\n");
      if (getchar() == 'q')
        break;
    }
  }

  finalize_fabric();

  return 0;
}
