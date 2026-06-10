import { createReadStream, existsSync, statSync } from 'node:fs'
import { createServer } from 'node:http'
import { extname, join, normalize } from 'node:path'

const host = '0.0.0.0'
const port = 7456
const root = new URL('.', import.meta.url).pathname
const types = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml'
}

function resolvePath(url) {
  const pathname = decodeURIComponent(new URL(url, `http://${host}:${port}`).pathname)
  const cleanPath = normalize(pathname).replace(/^(\.\.[/\\])+/, '')
  const filePath = join(root, cleanPath === '/' ? 'index.html' : cleanPath)

  if (!filePath.startsWith(root)) {
    return join(root, 'index.html')
  }

  if (existsSync(filePath) && statSync(filePath).isFile()) {
    return filePath
  }

  return join(root, 'index.html')
}

createServer((req, res) => {
  const filePath = resolvePath(req.url || '/')
  res.setHeader('Content-Type', types[extname(filePath)] || 'application/octet-stream')
  res.setHeader('Cache-Control', 'no-store')
  createReadStream(filePath).pipe(res)
}).listen(port, host, () => {
  console.log(`TG Mini App Vue server: http://192.168.1.6:${port}/`)
})
