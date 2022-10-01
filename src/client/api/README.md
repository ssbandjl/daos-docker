# DAOS Client Library

The DAOS API is divided along several functionalities to address the different
features that DAOS exposes:

DAOS API 分为几个功能，以解决不同的DAOS 公开的功能：

- Management API: pool and target management
- Pool Client API: pool access
- Container API: container management and access, container snapshots
- Transaction API: transaction model and concurrency control
- Object, Array and KV APIs: object and data management and access
- Event, Event Queue, and Task API: non-blocking operations
- Addons API: array and KV operations built over the DAOS object API
- DFS API: DAOS file system API to emulate a POSIX namespace over DAOS
- DUNS API: DAOS unified namespace API for integration with an existing system
  namespace.
- 管理 API：池和目标管理
- 池客户端 API：池访问
- 容器API：容器管理和访问、容器快照
- 事务API：事务模型和并发控制
- 对象、数组和 KV API：对象和数据管理和访问
- 事件、事件队列和任务 API：非阻塞操作
- 插件 API：基于 DAOS 对象 API 构建的数组和 KV 操作
- DFS API：DAOS 文件系统 API，用于在 DAOS 上模拟 POSIX 命名空间
- DUNS API：DAOS 统一命名空间 API，用于与现有系统集成
  命名空间。

Each of those components have associated README.md files that provide more
details about the functionality they support except for APIs to support
non-blocking operations which is discussed here.

这些组件中的每一个都有相关的 README.md 文件，这些文件提供了更多
除了要支持的 API 之外，它们支持的功能的详细信息这里讨论的非阻塞操作。

The libdaos API is available under [/src/include/daos\_\*](/src/include/) and
associated man pages under [/docs/man/man3/](/docs/man/man3/).

## Event & Event Queue

DAOS API functions can be used in either blocking or non-blocking mode. This is
determined through a pointer to a DAOS event passed to each API call that:

- if NULL indicates that the operation is to be blocking. The operation will
  return after completing the operation. The error codes for all failure cases
  will be returned through the return code of the API function itself.
- if a valid event is used, the operation will run in non-blocking mode and
  return immediately after scheduling the operation in the internal scheduler
  and after RPCs are submitted to the underlying stack. The return value of the
  operation is success if the scheduling succeeds, but does not indicate that
  the actual operation succeeds. The errors that can be caught on return are
  either invalid parameters or scheduling problems. The actual return code of
  the operation will be available in the event error code (event.ev_error) when
  the event completes.

A valid event to be used must be created first with a separate API call. To allow users to track
multiple events at a time, an event can be created as part of an event queue, which is basically a
collection of events that can be progressed and polled together. An event queue also creates a
separate task scheduler internally for all DAOS tasks as well as a new network context. The network
context creation is an expensive operation on some network providers, and thus users should try to
limit the number of event queue being created in their applications or IO middleware libraries on
top of DAOS. Alternatively, an event can be created without an event queue, and be tracked
individually. In this case, and for blocking operations, an internal global task scheduler and
network context is used instead for the independent ones that would be created for an event
queue. Once an event is completed, it can re-used for another DAOS API call to minimize the need for
event creation and allocations inside the DAOS library.

## Task Engine Integration

The DAOS Task API provides an alternative way to use the DAOS API in a
non-blocking manner and at the same time build a task dependency tree between
DAOS API operation. This is useful for applications and middleware libraries
using DAOS and needing to build a schedule of DAOS operations with dependencies
between each other (N-1, 1-N, N-N).

To leverage the task API, the user would need to create a scheduler where DAOS
tasks can be created as a part of. The task API is generic enough to allow the
user to mix DAOS specific tasks (through the DAOS task API) and other user
defined tasks and add dependencies between those.

For more details on how TSE is used in client library, see [TSE internals
documentation](/src/common/README.md) for more details.
