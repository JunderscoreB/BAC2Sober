Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready!');
});

Pebble.addEventListener('appmessage', function(e) {
  var dict = e.payload;
  if ('SOBER_TIME' in dict) {
    var soberTime = dict['SOBER_TIME'];
    var targetBac = ('TARGET_BAC' in dict) ? (dict['TARGET_BAC'] / 100).toFixed(2) : "0.00";

    if (soberTime > 0) {
      pushTimelinePin(soberTime, targetBac);
    }
  }
});

function pushTimelinePin(timestamp, targetBac) {
  Pebble.getTimelineToken(function(token) {
    var date = new Date(timestamp * 1000);

    var pin = {
      "id": "bac2sober-target-pin",
      "time": date.toISOString(),
                          "layout": {
                            "type": "genericPin",
                            "title": "BAC " + targetBac,
                            "body": "Your blood alcohol content has reached your target level.",
                            "tinyIcon": "system://images/NOTIFICATION_FLAG"
                          }
    };

    var request = new XMLHttpRequest();
    request.open('PUT', 'https://timeline-api.rebble.io/v1/user/pins/' + pin.id, true);
    request.setRequestHeader('Content-Type', 'application/json');
    request.setRequestHeader('X-User-Token', token);
    request.send(JSON.stringify(pin));

  }, function(error) {
    console.error('Error getting timeline token: ' + error);
  });
}
