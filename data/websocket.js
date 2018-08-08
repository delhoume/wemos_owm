var connection = new WebSocket('ws://' + location.hostname + ':81/');

connection.onopen = function () {
    getValues();
};

connection.onerror = function (error) {
    console.log('WebSocket Error ', error);
};

var options = { year: 'numeric', month: 'long', weekday: 'long', day: 'numeric',
                hour:'numeric', minute:'numeric', second:'numeric'};

var h = document.getElementById('h');
var t = document.getElementById('t');
var d = document.getElementById('d');

connection.onmessage = function(evt) {
     var info = JSON.parse(evt.data);
     t.textContent = "&#x1f321; " + info.t + "&deg;";
     h.textContent = "&#x1f4a7; " + info.h + "%";
     d.textContent = new Date().toLocaleString(undefined, options); 
}

window.setInterval(getValues, 5000);

function getValues() {
    connection.send('ping');
}
     
