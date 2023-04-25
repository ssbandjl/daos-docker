cd build/external/debug/mercury.build
cmake -DMERCURY_USE_CHECKSUMS=OFF -DOPA_LIBRARY=/opt/daos/prereq/debug/openpa/lib/libopa.a -DOPA_INCLUDE_DIR=/opt/daos/prereq/debug/openpa/include/ -DCMAKE_INSTALL_PREFIX=/opt/daos/prereq/debug/mercury -DBUILD_EXAMPLES=OFF -DMERCURY_USE_BOOST_PP=ON -DMERCURY_ENABLE_DEBUG=ON -DBUILD_TESTING=OFF -DNA_USE_OFI=ON -DBUILD_DOCUMENTATION=OFF -DBUILD_SHARED_LIBS=ON ../mercury -DOFI_INCLUDE_DIR=/opt/daos/prereq/debug/ofi/include -DOFI_LIBRARY=/opt/daos/prereq/debug/ofi/lib/libfabric.so
make install
