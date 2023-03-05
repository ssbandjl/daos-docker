# DFS Overview

DFS stands for DAOS File System. The DFS API provides an encapsulated namespace
with a POSIX-like API directly on top of the DAOS API. The namespace is
encapsulated under a single DAOS container, where directories and files are
objects in that container.

The encapsulated namespace will be located in one DAOS Pool and a single DAOS
Container. The user provides a valid (connected) pool handle and an open
container handle where the namespace will be located.

## DFS Namespace

When the file system is created (i.e. when the DAOS container is initialized as
an encapsulated namespace), a reserved object (with a predefined object ID) will
be added to the container and will record superblock (SB) information about the
namespace. The SB object has the reserved OID 0.0. The object class is
determined either through the oclass parameter passed to container creation or
through automatic selection based on container properties such as the redundancy
factor.

The SB object contains an entry with a magic value to indicate it is a POSIX
filesystem. The SB object will contain also an entry to the root directory of
the filesystem, which will be another reserved object with a predefined OID
(1.0) and will have the same representation as a directory (see next
section). The OID of the root id will be inserted as an entry in the superblock
object.

SB 对象包含一个带有魔法值的条目，表明它是一个 POSIX 文件系统。 SB 对象还将包含文件系统根目录的条目，这将是另一个具有预定义 OID (1.0) 的保留对象，并将具有与目录相同的表示形式（请参阅下一节）。 root id 的 OID 将作为条目插入到 superblock 对象中。

The SB will look like this:

~~~~
D-key: "DFS_SB_METADATA"
A-key: "DFS_MAGIC"
single-value (uint64_t): SB_MAGIC (0xda05df50da05df50)

A-key: "DFS_SB_VERSION"
single-value (uint16_t): Version number of the SB. This is used to determine the layout of the SB (the DKEYs and value sizes).

A-key: "DFS_LAYOUT_VERSION"
single-value (uint16_t): This is used to determine the format of the entries in the DFS namespace (DFS to DAOS mapping).

A-key: "DFS_SB_FEAT_COMPAT"
single-value (uint64_t): flags to indicate feature set like extended attribute support, indexing

A-key: "DFS_SB_FEAT_INCOMPAT"
single-value (uint64_t): flags

A-key: "DFS_SB_MKFS_TIME"
single-value (uint64_t): time when DFS namespace was created

A-key: "DFS_SB_STATE"
single-value (uint64_t): state of FS (clean, corrupted, etc.)

A-key: "DFS_CHUNK_SIZE"
single-value (uint64_t): Default chunk size for files in this container

A-key: "DFS_OBJ_CLASS"
single-value (uint16_t): Default object class for files in this container

D-key: "/"
// rest of akey entries for root are same as in directory entry described below.
~~~~~~

## DFS Directories

A POSIX directory will map to a DAOS object with multiple dkeys, where each dkey
will correspond to an entry in that directory (for another subdirectory, regular
file, or symbolic link). The dkey value will be the entry name in that
directory. The dkey will contain an akey with all attributes of that entry in a
byte array serialized format. Extended attributes will each be stored in a
single value under a different akey. The mapping table will look like this
(includes two extended attributes: xattr1, xattr2):

POSIX 目录将映射到具有多个 dkey 的 DAOS 对象，其中每个 dkey 将对应于该目录中的一个条目（对于另一个子目录、常规文件或符号链接）。 dkey 值将是该目录中的条目名称。 dkey 将包含一个 akey，该 akey 具有字节数组序列化格式的该条目的所有属性。 扩展属性将分别存储在不同键下的单个值中。 映射表将如下所示（包括两个扩展属性：xattr1、xattr2）：
     目录(dir)
      |
    / |  \
dkey dkey dkey(entry,subdir/file/symblic_link)
 |    |     |
akey akey  akey(all_attributes)

dkey_value = entry_name


~~~~~~
Directory Object
  D-key "entry1_name"
    A-key "DFS_INODE"
      RECX (byte array starting at idx 0):
        mode_t: permission bit mask + type of entry
        oid: object id of entry
        atime: access time
        mtime: modify time
        ctime: change time
        chunk_size: chunk_size of file (0 if default or not a file)
        syml: symlink value (does not exist if not a symlink)
    A-key "x:xattr1"	// extended attribute name (if any)
    A-key "x:xattr2"	// extended attribute name (if any)
~~~~~~

The extended attributes are all prefixed with "x:".

This summarizes the mapping of a directory testdir with a file, directory, and
symlink:

~~~~~~
testdir$ ls
dir1
file1
syml1 -> dir1

Object testdir
  D-key "dir1"
    A-key "mode" , permission bits + S_IFDIR
    A-key "oid" , object id of dir1
    ...
  D-key "file1"
    A-key "mode" , permission bits + S_IFREG
    A-key "oid" , object id of file1
    ...
  D-key "syml1"
    A-key "mode" , permission bits + S_IFLNK
    A-key "oid" , empty
    A-key "syml", dir1
    ...
~~~~~~

By default, all directories are created with an object class with 1 shard. This means, that if the
container redundancy factor (RF) is 0, OC_S1 oclass will be used; if RF=1 OC_RP_2G1 is used, and so
on. The user can of course change that when creating the directory and set the desired object class
manually, or set the default object class when creating the container.

Note that with this mapping, the inode information is stored with the entry that it corresponds to
in the parent directory object. Thus, hard links won't be supported, since it won't be possible to
create a different entry (dkey) that actually points to the same set of akeys that the current ones
are stored within. This limitation makes the representation simple as described above.

请注意，对于此映射，inode 信息与其对应的条目一起存储在父目录对象中。 因此，将不支持硬链接，因为不可能创建一个不同的条目 (dkey)，它实际上指向存储当前密钥的同一组 akeys。 这种限制使表示变得简单，如上所述。

## Files

As shown in the directory mapping above, the entry of a file will be inserted in
its parent directory object with an object ID that corresponds to that file. The
object ID for a regular file will be of a DAOS array object, which itself is a
DAOS object with some properties (the element size and chunk size). In the
POSIX file case, the cell size will always be 1 byte. The chunk size can be set
at create time only, with the default being 1 MB. The array object itself is
mapped onto a DAOS object with integer dkeys, where each dkey contains
chunk_size elements. So for example, if we have a file with size 10 bytes, and
chunk size is 3 bytes, the array object will contain the following:

如上面的目录映射所示，文件的条目将插入到其父目录对象中，对象 ID 对应于该文件。 常规文件的对象 ID 是一个 DAOS 数组对象，它本身是一个具有某些属性（元素大小和块大小）的 DAOS 对象。 在 POSIX 文件情况下，单元格大小始终为 1 个字节。 块大小只能在创建时设置，默认为 1 MB。 数组对象本身映射到具有整数 dkey 的 DAOS 对象，其中每个 dkey 包含 chunk_size 元素。 因此，例如，如果我们有一个大小为 10 字节的文件，并且块大小为 3 字节，则数组对象将包含以下内容：

~~~~
Object array
  D-key 0
    A-key NULL , array elements [0,1,2]
  D-key 1
    A-key NULL , array elements [3,4,5]
  D-key 2
    A-key NULL , array elements [6,7,8]
  D-key 3
    A-key NULL , array elements [9]
~~~~~~

For more information about the array object layout, please refer to the
README.md file for Array Addons.

By default, all files are created with an object class with max sharding. This means, that if the
container redundancy factor (RF) is 0, OC_SX oclass will be used; if RF=1 an EC oclass OC_EC_nP1GX
is used, and so on. The user can of course change that when creating the file and set the desired
object class manually, or set the default object class when creating the container.

Access to that object is done through the DAOS Array API. All read and write
operations to the file will be translated to DAOS array read and write
operations. The file size can be set (truncate) or retrieved by the DAOS array
set_size/get_size functions. Increasing the file size however in this case, does not
guarantee that space is allocated. Since DAOS logs I/Os across different epochs,
space allocation cannot be supported by a naïve set_size operation.
对该对象的访问是通过 DAOS Array API 完成的。 所有对文件的读写操作都会被翻译成DAOS数组的读写操作。 可以通过 DAOS 数组 set_size/get_size 函数设置（截断）或检索文件大小。 然而，在这种情况下，增加文件大小并不能保证分配空间。 由于 DAOS 记录不同时期的 I/O，因此简单的 set_size 操作无法支持空间分配。

## Symbolic Links

As mentioned in the directory section, symbolic links will not have an object
for the symlink itself, but will have a value in the entry itself of the parent
directory containing the actual value of the symlink.

正如在目录部分中提到的，符号链接不会有符号链接本身的对象，但会在包含符号链接实际值的父目录的条目本身中有一个值。

## Access Permissions

All DFS objects (files, directories, and symlinks) inherit the access
permissions of the DFS container that they are created with. So the permission
checks are done on dfs_mount(). If that succeeds and the user has access to the
container, then they will be able to access all objects in the DFS
namespace.

setuid(), setgid() programs, supplementary groups, ACLs are not supported in the
DFS namespace.
所有 DFS 对象（文件、目录和符号链接）都继承创建它们的 DFS 容器的访问权限。 所以权限检查是在 dfs_mount() 上完成的。 如果成功并且用户可以访问容器，那么他们将能够访问 DFS 命名空间中的所有对象。

DFS 命名空间不支持 setuid()、setgid() 程序、补充组、ACL

