#deps

# LIB=''

# if [ $# -eq 2 ];then
#   LIB=$1
# fi

# function build_ofi(){
#   sed -i "s/== 'ofi1'/== 'ofi'/g" site_scons/prereq_tools/base.py
# }
# function build_mercury(){
#   sed -i "s/== 'mercury1'/== 'mercury'/g" site_scons/prereq_tools/base.py
# }
# function no_build_ofi(){
#   sed -iE "s/== 'ofi'/== 'ofi1'/g" site_scons/prereq_tools/base.py
# }
# function no_build_mercury(){
#   sed -iE "s/== 'mercury'/== 'mercury1'/g" site_scons/prereq_tools/base.py
# }



# case "$LIB" in
# ofi*)
#   echo "ofi"
#   build_ofi
#   ;;
# mer*)
#   echo "mercury"
#   build_mercury
#   ;;
# *)
#   ;;
# esac

scons-3 --build-deps=yes --jobs 1 PREFIX=/opt/daos TARGET_TYPE=debug --deps-only


# #ofi
# cd build/external/debug/ofi && make && make install

# #mercury
# cd build/external/debug/mercury.build
# cmake -DMERCURY_USE_CHECKSUMS=OFF -DOPA_LIBRARY=/opt/daos/prereq/debug/openpa/lib/libopa.a -DOPA_INCLUDE_DIR=/opt/daos/prereq/debug/openpa/include/ -DCMAKE_INSTALL_PREFIX=/opt/daos/prereq/debug/mercury -DBUILD_EXAMPLES=OFF -DMERCURY_USE_BOOST_PP=ON -DMERCURY_ENABLE_DEBUG=ON -DBUILD_TESTING=OFF -DNA_USE_OFI=ON -DBUILD_DOCUMENTATION=OFF -DBUILD_SHARED_LIBS=ON ../mercury -DOFI_INCLUDE_DIR=/opt/daos/prereq/debug/ofi/include -DOFI_LIBRARY=/opt/daos/prereq/debug/ofi/lib/libfabric.so
# make install