#!/usr/bin/env python

import urllib2
import os
import sys
import zipfile

try:
	import cStringIO as StringIO
except:
	import StringIO


SRC = "http://www.3gpp.org/ftp/Specs/archive/06_series/06.06/0606-421.zip"


def get_zipfile(data):
	return zipfile.ZipFile(StringIO.StringIO(data))


def get_subfile_data(data, filename):
	z = get_zipfile(data)
	return z.read(filename)


def process_file(z, e):
	fh = open(e.filename.lower(), 'w')
	d = z.read(e).replace('\r','')
	fh.write(d)
	fh.close()


def main(*args):

	# Args
	if len(args) != 2:
		print "Usage: %s target_dir" % args[0]
		return

	tgt = args[1]

	# Create and go to target dir
	if not os.path.isdir(tgt):
		os.mkdir(tgt)
	os.chdir(tgt)

	# Get the original data
	u = urllib2.urlopen(SRC)
	d = u.read()

	# Get DISK.zip
	d = get_subfile_data(d, 'DISK.zip')

	# Get Dir_C.zip
	d = get_subfile_data(d, 'Dir_C.zip')

	# Get zip file object
	z = get_zipfile(d)

	# Save each file
	for e in z.filelist:
		process_file(z, e)


if __name__ == '__main__':
	main(*sys.argv)
