#!/bin/bash
# vmopen.sh <ns2|ns3|ns4|ie4|kill> [url]: opens the URL in that browser of the XP guest through Start, Run
# (the guest is 640x480: Start at 40,465, Run at 238,400), or with "kill" ends every browser (taskkill), which
# closes all windows and error dialogs.  The URL is typed, so only letters, digits and . / : - _ ? = & are used.
cd /Users/alex/VMs/ie6-xp || exit 1
case "$1" in
  ns2) exe='C:\NS202\Program\netscape.exe' ;;
  ns3) exe='C:\NS304\Program\netscape.exe' ;;
  ns4) exe='C:\NS408\Program\netscape.exe' ;;
  ie4) exe='"C:\Program Files\Utilu IE Collection\IE401\iexplore.exe"' ;;
  kill) exe='' ;;
  *) echo "usage: vmopen.sh ns2|ns3|ns4|ie4|kill [url]"; exit 1 ;;
esac
python3 vm.py tap 40 465; sleep 3; python3 vm.py tap 238 400; sleep 3
if [ "$1" = kill ]; then
  python3 vm.py keys "taskkill /f /im netscape.exe"; sleep 1; python3 vm.py key ret; sleep 5
  python3 vm.py tap 40 465; sleep 3; python3 vm.py tap 238 400; sleep 3
  python3 vm.py keys "taskkill /f /im iexplore.exe"; sleep 1; python3 vm.py key ret; sleep 5
else
  python3 vm.py keys "$exe $2"; python3 vm.py key ret
fi
