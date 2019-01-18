#!/bin/sh

if [ "$UID" -ne 0 ]; then
    echo "Run this script as root."
    exit 1
fi

while ! pidof memcached &>/dev/null; do
    sleep 0.1
done

memcached_parent=($(ps -o ppid= -p $(ps -o ppid= -p $(pidof memcached))))
memcached_parent=${memcached_parent[0]}

cleanup() {
    rmdir /var/run/sam/${memcached_parent}
    rmdir /sys/fs/cgroup/cpuset/sam/app-${memcached_parent}
    exit 0
}

trap cleanup TERM EXIT INT QUIT

mkdir -p /sys/fs/cgroup/cpuset/sam/app-${memcached_parent}
cat /sys/fs/cgroup/cpuset/sam/cpuset.cpus > /sys/fs/cgroup/cpuset/sam/app-${memcached_parent}/cpuset.cpus
if [ "$?" -ne 0 ]; then
    exit 1
fi

cat /sys/fs/cgroup/cpuset/sam/cpuset.mems > /sys/fs/cgroup/cpuset/sam/app-${memcached_parent}/cpuset.mems
if [ "$?" -ne 0 ]; then
    exit 1
fi

echo ${memcached_parent} > /sys/fs/cgroup/cpuset/sam/app-${memcached_parent}/tasks
if [ "$?" -ne 0 ]; then
    exit 1
fi

mkdir -p /var/run/sam/${memcached_parent}
echo "memcached + ferret w/ SAM-MAP ..."
sudo -u $SUDO_USER ./jobtest -n 5 -f workloads/ferret.txt

# echo "(Standalone with SAM-MAP) Waiting for SIGINT ..."
# while true; do sleep 1; done
