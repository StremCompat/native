'use strict';

// For development and the screenshot tests: the real local server of StremCompat (from the
// public-repo folder) with a made-up addon and account, so the native interface has something
// to show without anyone's account. Pictures are drawn here as plain gradients.
//
// Usage: node tools/dev-server.js <public-repo folder>     (Ctrl+C stops both servers)

var childProcess = require('child_process');
var fs = require('fs');
var http = require('http');
var os = require('os');
var path = require('path');
var zlib = require('zlib');

var repo = path.resolve(process.argv[2] || path.join(__dirname, '..', '..', 'LG', 'public-repo'));
var ADDON_PORT = 9294;
var base = 'http://127.0.0.1:' + ADDON_PORT;
var appPort = require(path.join(repo, 'service', 'profile.js')).current.port;

/* ---------- pictures: a PNG of two colours, top to bottom ---------- */
var crcTable = [];
for (var n = 0; n < 256; n++) {
  var c = n;
  for (var k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  crcTable[n] = c >>> 0;
}
function crc32(buf) {
  var c = 0xffffffff;
  for (var i = 0; i < buf.length; i++) c = crcTable[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  var len = Buffer.alloc(4),
    crc = Buffer.alloc(4),
    body = Buffer.concat([Buffer.from(type), data]);
  len.writeUInt32BE(data.length);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}
function png(width, height, top, bottom) {
  var raw = Buffer.alloc((width * 3 + 1) * height);
  for (var y = 0; y < height; y++) {
    var t = y / (height - 1),
      at = y * (width * 3 + 1);
    for (var x = 0; x < width; x++) {
      var stripe = (x + y) % 80 < 6 ? 0.85 : 1;
      for (var ch = 0; ch < 3; ch++)
        raw[at + 1 + x * 3 + ch] = Math.round((top[ch] * (1 - t) + bottom[ch] * t) * stripe);
    }
  }
  var ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0))
  ]);
}
var palette = [[180, 60, 40], [40, 90, 170], [60, 140, 90], [150, 120, 40], [110, 60, 150], [40, 130, 140]];

/* ---------- the made-up addon ---------- */
var NAMES = ['The Long Night', 'Harbor Lights', 'Paper Moons', 'Red Valley', 'Quiet Signal', 'Northern Line',
  'Glass House', 'The Ninth Room', 'Salt and Iron', 'Morning Static', 'Low Tide', 'Ember Road'];
function title(i, type) {
  return {
    id: 'dev' + type[0] + i,
    type: type,
    name: NAMES[i % NAMES.length] + (i >= NAMES.length ? ' ' + (Math.floor(i / NAMES.length) + 1) : ''),
    poster: base + '/poster/' + i + '.png',
    background: base + '/backdrop/' + i + '.png',
    description: 'A made-up title for trying the interface: a long enough synopsis to wrap over two lines and ' +
      'show how the hero cuts it with an ellipsis when there is more to say than fits.',
    genres: ['Drama', 'Mystery', 'Thriller'],
    runtime: 90 + i + ' min',
    releaseInfo: String(2010 + (i % 15)),
    imdbRating: (6 + (i % 4) * 0.7).toFixed(1)
  };
}
var manifest = {
  id: 'dev.addon',
  name: 'Development',
  version: '1.0.0',
  resources: ['catalog', 'meta'],
  types: ['movie', 'series'],
  idPrefixes: ['dev'],
  catalogs: [
    { type: 'movie', id: 'top', name: 'Popular' },
    { type: 'series', id: 'trending', name: 'Trending' },
    { type: 'movie', id: 'new', name: 'New' }
  ]
};
function catalog(type, offset) {
  var out = [];
  for (var i = 0; i < 14; i++) out.push(title(i + offset, type));
  return { metas: out };
}
var answers = {
  '/manifest.json': manifest,
  '/catalog/movie/top.json': catalog('movie', 0),
  '/catalog/series/trending.json': catalog('series', 3),
  '/catalog/movie/new.json': catalog('movie', 6)
};

var addon = http.createServer(function (req, res) {
  var url = req.url.split('?')[0],
    pic = /^\/(poster|backdrop)\/(\d+)\.png$/.exec(url),
    meta = /^\/meta\/(movie|series)\/dev[ms](\d+)\.json$/.exec(url);
  if (pic) {
    var i = Number(pic[2]),
      color = palette[i % palette.length],
      dark = color.map(function (v) {
        return Math.round(v * 0.3);
      });
    res.writeHead(200, { 'Content-Type': 'image/png' });
    return res.end(pic[1] === 'poster' ? png(300, 450, color, dark) : png(1280, 720, color, dark));
  }
  var answer = meta ? { meta: title(Number(meta[2]), meta[1]) } : answers[url];
  res.writeHead(answer ? 200 : 404, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify(answer || { error: 'not found' }));
});

/* ---------- the made-up account, handed over as the web interface would ---------- */
function account() {
  var now = Date.now(),
    state = {
      authKey: JSON.stringify('dev-key'),
      user: JSON.stringify({ _id: 'dev1', email: 'dev@example.com' }),
      addons: JSON.stringify([{ transportUrl: base + '/manifest.json', manifest: manifest }])
    };
  [2, 5, 8].forEach(function (i, n) {
    var it = title(i, 'movie');
    state['lib_dev1_' + it.id] = JSON.stringify({
      _id: it.id,
      type: 'movie',
      name: it.name,
      poster: it.poster,
      background: it.background,
      state: { timeOffset: (20 + n * 25) * 60000, duration: 110 * 60000, video_id: it.id },
      _mtime: new Date(now - n * 1000).toISOString()
    });
  });
  return state;
}

function post(pathName, body, cb) {
  var req = http.request(
    { host: '127.0.0.1', port: appPort, path: pathName, method: 'POST', headers: { 'Content-Type': 'application/json' } },
    function (res) {
      res.resume();
      res.on('end', cb);
    }
  );
  req.on('error', cb);
  req.end(JSON.stringify(body));
}

addon.listen(ADDON_PORT, '127.0.0.1', function () {
  var home = fs.mkdtempSync(path.join(os.tmpdir(), 'stremcompat-dev-'));
  var server = childProcess.spawn(process.execPath, [path.join(repo, 'service', 'local-server.js')], {
    cwd: repo,
    stdio: 'inherit',
    env: Object.assign({}, process.env, { HOME: home, SC_CINEMETA_URL: base, SC_SERVER_LOG: path.join(home, 'server.log') })
  });
  function handOver(tries) {
    post('/app/migrate', account(), function (err) {
      if (err && tries > 0) return setTimeout(handOver, 200, tries - 1);
      console.log(err ? 'the local server did not start' : 'ready on :' + appPort);
    });
  }
  handOver(50);
  process.on('SIGINT', function () {
    server.kill();
    process.exit(0);
  });
});
