import sys
from host import *
from flask import Flask
import json
import os

app = Flask(__name__)

def on_blocks(localID_, blocks):
	global host, IDs, localID
	localID = localID_
	IDs = []
	print(blocks)
	for _, data in blocks.items():
		for ID in data:
			if ID != 0:
				IDs.append(str(ID))

localID = None
IDs = None
host = None

def return_json(obj = None):
	return app.response_class(response=json.dumps(obj) if obj else "", status=200, mimetype="application/json")

@app.route("/init_download")
def init_download():
	global host, IDs
	IDs = None
	host = Host(os.environ["DEV_PATH"], 115200, on_blocks)
	host.start()
	return return_json()

@app.route("/results")
def results():
	global host, IDs
	if IDs is not None:
		if host:
			host.stop()
			host = None
		return return_json({"ids": IDs, "localID": str(localID)})
	else:
		return return_json()

@app.after_request
def after_request(response):
    header = response.headers
    header['Access-Control-Allow-Origin'] = '*'
    header['Access-Control-Allow-Headers'] = 'Content-Type, Authorization'
    header['Access-Control-Allow-Methods'] = 'OPTIONS, HEAD, GET, POST, DELETE, PUT'
    return response
