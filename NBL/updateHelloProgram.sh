./mounthdd.sh
cd ../user
make
cd ../
rm hdd/hello.bin
cp user/hello.bin hdd/
cd NBL
sudo umount ../hdd
