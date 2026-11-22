#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null
echo "TZD-BOOT-OK kernel $(uname -r)"
ip link set lo up
ip link set eth0 up && ip addr add 10.0.2.15/24 dev eth0 && ip route add default via 10.0.2.2
ip addr show eth0 | grep 'inet '
echo '<html>hello from the guest</html>' > /www/index.html
httpd -p 80 -h /www
echo "TZD-NET-READY"
# stay up for the host-side check, then power off
sleep 20
echo "TZD-POWEROFF"
poweroff -f
