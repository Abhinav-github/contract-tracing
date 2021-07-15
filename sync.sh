cd /home/glaba/Desktop/ece445/projects
inotifywait -r -m -e close_write --format '%w%f' /home/glaba/Desktop/ece445/projects | while read MODFILE
do
	cp contacts.* RangingTag/
	cp contacts.* RangingAnchor/
	cp usb.* RangingTag/
	cp usb.* RangingAnchor/
done
