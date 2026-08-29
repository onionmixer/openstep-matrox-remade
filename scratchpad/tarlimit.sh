#!/bin/sh
# Is installer_tar's limit inclusive of 100 characters, or below it?
# A rename that lands exactly on 100 is only safe if the answer is inclusive.
R=/tmp/tl
rm -rf $R; mkdir $R; mkdir $R/d
awk 'BEGIN{for(i=99;i<=101;i++){p=i-4-4;s="";for(k=0;k<p;k++)s=s"x";printf "%s_%03d\n", s, i}}' > /tmp/names
while read n; do echo hi > "$R/d/$n"; done < /tmp/names
cd $R
/NextAdmin/Installer.app/installer_tar cf /tmp/tl.tar . 2> /tmp/tl.err
echo "--- stderr ---"; cat /tmp/tl.err
echo "--- survived ---"
/NextAdmin/Installer.app/installer_tar tf /tmp/tl.tar | awk 'length($0)>90 { print length($0), $0 }'
