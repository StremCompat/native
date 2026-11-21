'use strict';

// For development and the screenshot tests: the real local server of StremCompat (from the
// public-repo folder) with a made-up addon and account, so the native interface has something
// to show without anyone's account. Pictures are drawn here as plain gradients.
//
// Usage: node tools/dev-server.js <public-repo folder> [--signed-out]     (Ctrl+C stops both servers)

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
  resources: ['catalog', 'meta', 'stream', 'subtitles'],
  types: ['movie', 'series', 'tv'],
  idPrefixes: ['dev'],
  catalogs: [
    { type: 'movie', id: 'top', name: 'Popular' },
    { type: 'series', id: 'trending', name: 'Trending' },
    { type: 'movie', id: 'new', name: 'New' },
    { type: 'tv', id: 'live', name: 'Live' }
  ]
};
/* two seasons, the last episode still to air */
function withEpisodes(t) {
  if (t.type !== 'series') return t;
  var day = 86400000,
    start = Date.now() - 70 * day;
  t.videos = [];
  [8, 6].forEach(function (count, s) {
    for (var e = 1; e <= count; e++)
      t.videos.push({
        id: t.id + ':' + (s + 1) + ':' + e,
        season: s + 1,
        episode: e,
        title: 'Chapter ' + (s * 8 + e),
        overview: 'What happens in episode ' + e + ' of season ' + (s + 1) + ', in one or two sentences.',
        thumbnail: base + '/backdrop/' + (s * 8 + e) + '.png',
        /* IMDb's rating, as Cinemeta gives it; the third episode has none */
        rating: e === 3 ? undefined : (7 + (e % 4) * 0.6).toFixed(1),
        released: new Date(start + (s * 8 + e) * 7 * day).toISOString()
      });
  });
  return t;
}
/* sources as torrent addons name them: quality, languages, size and seeds in the text */
var STREAMS = [
  ['1080p', 'The.Title.2014.1080p.BluRay.x264.DDP5.1\n\ud83d\udc64 1320 \ud83d\udcbe 2.6 GB', 1320],
  ['1080p', 'The.Title.2014.IMAX.1080p.WEB-DL.Multi.Subs\nSubs: Portuguese, English\n\ud83d\udc64 650 \ud83d\udcbe 4.2 GB', 650],
  ['1080p', 'The.Title.2014.1080p.WEB-DL.Dual.Audio.Portuguese.English\n\ud83d\udc64 402 \ud83d\udcbe 3.1 GB', 402],
  ['4K', 'The.Title.2014.2160p.UHD.BluRay.REMUX.DV.HDR.Atmos.7.1\n\ud83d\udc64 120 \ud83d\udcbe 61.2 GB', 120],
  ['720p', 'The.Title.2014.720p.BluRay.x264\n\ud83d\udc64 88 \ud83d\udcbe 1.1 GB', 88]
].map(function (s, i) {
  return {
    name: 'Development\n' + s[0],
    title: s[1],
    infoHash: ('0000000000000000000000000000000000000000' + (i + 1)).slice(-40),
    fileIdx: 0
  };
});
function catalog(type, offset) {
  var out = [];
  for (var i = 0; i < 14; i++) out.push(title(i + offset, type));
  return { metas: out };
}
var answers = {
  '/manifest.json': manifest,
  '/catalog/movie/top.json': catalog('movie', 0),
  '/catalog/series/trending.json': catalog('series', 3),
  '/catalog/movie/new.json': catalog('movie', 6),
  '/catalog/tv/live.json': {
    metas: ['News 24', 'Sports One', 'Movies Max', 'Kids Fun', 'Music Hits'].map(function (name, i) {
      return { id: 'devtv' + i, type: 'tv', name: name, logo: base + '/poster/' + i + '.png' };
    })
  }
};

var addon = http.createServer(function (req, res) {
  var url = req.url.split('?')[0],
    pic = /^\/(poster|backdrop)\/(\d+)\.png$/.exec(url),
    meta = /^\/meta\/(movie|series)\/dev[ms](\d+)\.json$/.exec(url);
  /* an M3U list for the Channels screen */
  if (url === '/list.m3u') {
    res.writeHead(200, { 'Content-Type': 'audio/x-mpegurl' });
    return res.end(
      '#EXTM3U\n' +
        ['World', 'Cinema', 'Weather']
          .map(function (n, i) {
            return '#EXTINF:-1 tvg-logo="' + base + '/poster/' + (i + 2) + '.png" group-title="Free TV",' + n + '\n' + base + '/live' + i + '.m3u8';
          })
          .join('\n') +
        '\n'
    );
  }
  if (pic) {
    var i = Number(pic[2]),
      color = palette[i % palette.length],
      dark = color.map(function (v) {
        return Math.round(v * 0.3);
      });
    res.writeHead(200, { 'Content-Type': 'image/png' });
    return res.end(pic[1] === 'poster' ? png(300, 450, color, dark) : png(1280, 720, color, dark));
  }
  var streams = /^\/stream\/(movie|series)\/[^/]+\.json$/.test(url);
  /* subtitles: two in Portuguese and one in English, each a file of its own */
  var sub = /^\/sub(\d)\.srt$/.exec(url);
  if (sub) {
    res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' });
    return res.end('1\n00:00:00,000 --> 01:00:00,000\nSubtitle ' + sub[1] + ': ninguém sabe o que aconteceu naquela noite.\n');
  }
  if (/^\/subtitles\/(movie|series)\//.test(url)) {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    return res.end(
      JSON.stringify({
        subtitles: [
          { id: 's1', url: base + '/sub1.srt', lang: 'por' },
          { id: 's2', url: base + '/sub2.srt', lang: 'pob' },
          { id: 's3', url: base + '/sub3.srt', lang: 'eng' }
        ]
      })
    );
  }
  /* search: every title of a type whose name has the words */
  var found = /^\/catalog\/(movie|series)\/top\/search=([^/]+)\.json$/.exec(url);
  if (found) {
    var words = decodeURIComponent(found[2]).toLowerCase();
    var metas = catalog(found[1], 0).metas.filter(function (m) {
      return m.name.toLowerCase().indexOf(words) >= 0;
    });
    res.writeHead(200, { 'Content-Type': 'application/json' });
    return res.end(JSON.stringify({ metas: metas }));
  }
  var answer = streams ? { streams: STREAMS } : meta ? { meta: withEpisodes(title(Number(meta[2]), meta[1])) } : answers[url];
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
  /* a followed series, for the calendar */
  var show = title(3, 'series');
  state['lib_dev1_' + show.id] = JSON.stringify({
    _id: show.id,
    type: 'series',
    name: show.name,
    poster: show.poster,
    background: show.background,
    removed: false,
    state: { timeOffset: 600000, duration: 2400000, video_id: show.id + ':1:2' },
    _mtime: new Date(now - 5000).toISOString()
  });
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
  /* --signed-out: no account, for the sign-in screen */
  var signedOut = process.argv.indexOf('--signed-out') >= 0;
  function handOver(tries) {
    post('/app/migrate', signedOut ? {} : account(), function (err) {
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
