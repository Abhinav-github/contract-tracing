from flask import Flask, request
import json

app = Flask(__name__)

class ContactNode:
	def __init__(self, ID, positive):
		self.ID = ID
		self.positive = positive
		self.edges = set()

class ContactGraph:
	def __init__(self):
		self.nodes = {}

	def markPositive(self, ID):
		if ID not in self.nodes:
			self.nodes[ID] = ContactNode(ID, True)
		else:
			self.nodes[ID].positive = True

	def addContacts(self, ID, contacts):
		if ID not in self.nodes:
			self.nodes[ID] = ContactNode(ID, False)
		self.nodes[ID].edges = self.nodes[ID].edges.union(contacts)

	def __mustQuarantineRec(self, ID, visited):
		for neighbor in self.nodes[ID].edges:
			if neighbor in visited:
				continue
			if neighbor not in self.nodes:
				continue
			if self.nodes[neighbor].positive:
				return True
			visited.add(neighbor)
			if self.__mustQuarantineRec(neighbor, visited):
				return True
		return False

	def mustQuarantine(self, ID):
		if ID not in self.nodes:
			return False
		elif self.nodes[ID].positive:
			return True

		visited = set([ID])
		return self.__mustQuarantineRec(ID, visited)

graph = ContactGraph()

def return_json(obj = None):
	return app.response_class(response=json.dumps(obj) if obj else "", status=200, mimetype="application/json")

@app.route("/positive/<ID>")
def positive(ID):
	global graph
	graph.markPositive(ID)
	return return_json()

@app.route("/upload_contacts/<ID>", methods=["POST"])
def upload_contacts(ID):
	global graph
	contacts = set(json.loads(next(iter(request.form.to_dict())))["contacts"])
	graph.addContacts(ID, contacts)
	return return_json()

@app.route("/must_quarantine/<ID>")
def must_quarantine(ID):
	global graph
	return return_json({"result": graph.mustQuarantine(ID)})

@app.after_request
def after_request(response):
    header = response.headers
    header['Access-Control-Allow-Origin'] = '*'
    header['Access-Control-Allow-Headers'] = 'Content-Type, Authorization'
    header['Access-Control-Allow-Methods'] = 'OPTIONS, HEAD, GET, POST, DELETE, PUT'
    return response
