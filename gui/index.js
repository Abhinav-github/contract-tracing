const LOCAL_SERVER = "http://localhost:5000";
const MAIN_SERVER = "http://localhost:5001";

let ID;

$(document).ready(() => {
	$.ajax(`${LOCAL_SERVER}/init_download`);
	pollResults();

	$("#uploadPositiveBtn").click(() => {
		$.ajax(`${MAIN_SERVER}/positive/${ID}`);
		console.log("doing this")
	});
});

const mustQuarantineCB = (data) => {
	$("#serverInfo").show();
	if (data.result) {
		$("#quarantine").text("You must quarantine!");
	} else {
		$("#quarantine").text("You don't have to quarantine!");
	}
};

let gotResults = false;
const pollResults = () => {
	if (gotResults)
		return;

	$.ajax(`${LOCAL_SERVER}/results`)
		.done((data) => {
			gotResults = true;
			$("#status").hide();
			$("#results").show();
			$("#uploadPositive").show();
			data.ids.forEach((id) => {
				$("#resultsTable")[0].innerHTML += `<tr><td>${id}</td></tr>`;
			});
			ID = data.localID;

			// Upload results to the server
			$.post(`${MAIN_SERVER}/upload_contacts/${ID}`, JSON.stringify({contacts: data.ids}));

			// Ask the server if we must quarantine
			$.ajax(`${MAIN_SERVER}/must_quarantine/${ID}`).done(mustQuarantineCB);
		});
};
setInterval(pollResults, 1000);