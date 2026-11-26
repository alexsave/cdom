#!/bin/bash
# vmns2.sh <browser ns2|ns3|ns4|ie4> <page and query> <wait s> <shot name>: through the guest agent (tools/vmagent.py, running in the
# guest as C:\a.exe), ends the browsers, opens http://10.0.2.2:8110/<page> in the browser (tests/out, or $VMDIR: a directory
# tools/vmserve.py serves, restarted each run), waits for the page to be drawn, saves shots/<name>.png.  No typing into the guest.
# A start that hangs (Netscape 2 sometimes stops at "Waiting for reply" with a grey view, and the server never sees the app
# document's request) is detected: the view's pixel at 550,250 is white once a page is drawn, and the server log must show
# the app document's GET; otherwise the browser is ended and the run retried, up to 4 times.  Exit 1 if it never drew.
b="$1"; page="$2"; wait="$3"; shot="$4"
here=$(cd "$(dirname "$0")" && pwd)
log=/Users/alex/VMs/ie6-xp/serve-frames-real.log
case "$b" in
    ns2) exe='C:\NS202\Program\netscape.exe' ;; ns3) exe='C:\NS304\Program\netscape.exe' ;; ns4) exe='C:\NS408\Program\netscape.exe' ;;
    ie4) exe='C:\Program Files\Utilu IE Collection\IE401\iexplore.exe' ;;
esac
# drawn: the view's pixel at 550,250 is white, the view is the same as 5 s before, and (the uttt board's grid line) the pixel at
# 87,300 is not white: a hung start is grey, and a page not yet drawn is blank white
white() { python3 -c "
from PIL import Image, ImageChops
a=Image.open('/Users/alex/VMs/ie6-xp/shots/.w.png').convert('RGB'); b=Image.open('/Users/alex/VMs/ie6-xp/shots/.w0.png').convert('RGB')
print(1 if a.getpixel((550,250))==(255,255,255) and a.getpixel((87,300))!=(255,255,255) and not ImageChops.difference(a.crop((0,60,620,420)),b.crop((0,60,620,420))).getbbox() else 0)"; }
for try in 1 2 3 4; do
    for p in netscape.exe iexplore.exe notepad.exe mshta.exe; do python3 "$here/vmagent.py" send "kill $p" >/dev/null; done
    sleep 4
    pkill -f vmserve.py; sleep 1
    (cd /Users/alex/VMs/ie6-xp && nohup python3 "$here/vmserve.py" "${VMDIR:-$here/../tests/out}" 8110 >> serve-frames-real.log 2>&1 &)
    sleep 1
    python3 "$here/vmagent.py" send "run \"$exe\" http://10.0.2.2:8110/r$(date +%s)/$page" >/dev/null
    t=0; drawn=0
    while [ $t -lt "$wait" ]; do
        sleep 5; t=$((t+5))
        cp /Users/alex/VMs/ie6-xp/shots/.w.png /Users/alex/VMs/ie6-xp/shots/.w0.png 2>/dev/null
        python3 /Users/alex/VMs/ie6-xp/vm.py shot /Users/alex/VMs/ie6-xp/shots/.w.png >/dev/null
        if [ $t -ge 15 ] && [ "$(white)" = 1 ]; then drawn=1; break; fi
        # no request for the app document 40 s after the start: hung before the frames
        if [ $t -ge 40 ] && ! grep -q "\.app\.html" <(tail -5 "$log"); then break; fi
    done
    if [ $drawn = 1 ]; then sleep 5; python3 /Users/alex/VMs/ie6-xp/vm.py shot "/Users/alex/VMs/ie6-xp/shots/$shot.png" >/dev/null; echo "drawn after $t s (try $try)"; exit 0; fi
    echo "try $try: hung or not drawn after $t s, retrying"
done
python3 /Users/alex/VMs/ie6-xp/vm.py shot "/Users/alex/VMs/ie6-xp/shots/$shot.png" >/dev/null
exit 1
