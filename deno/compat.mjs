// Minimal compatibility helpers for running tests under Deno.
export const fs = {
  readFileSync: (p) => Deno.readFileSync(p),
  writeFileSync: (p, data) => Deno.writeFileSync(p, (data instanceof Uint8Array) ? data : new TextEncoder().encode(String(data))),
  appendFileSync: (p, data) => {
    const enc = (data instanceof Uint8Array) ? data : new TextEncoder().encode(String(data));
    try { Deno.writeFileSync(p, enc, { append: true }); } catch (e) { Deno.writeFileSync(p, enc); }
  },
  existsSync: (p) => {
    try { return Deno.statSync(p).isFile || Deno.statSync(p).isDirectory; } catch { return false; }
  },
  mkdirSync: (p, opts) => Deno.mkdirSync(p, { recursive: !!(opts && opts.recursive) }),
  readdirSync: (p) => Array.from(Deno.readDirSync(p)).map(e => e.name),
  statSync: (p) => Deno.statSync(p)
};

export const path = {
  join: (...parts) => parts.join('/'),
  dirname: (p) => p.split('/').slice(0, -1).join('/') || '.',
  basename: (p) => p.split('/').pop()
};
export const crypto = {
  randomFillSync: (buf) => {
    // buf may be a Uint8Array or Buffer-like
    const u8 = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
    if (typeof globalThis.crypto !== 'undefined' && typeof globalThis.crypto.getRandomValues === 'function') {
      globalThis.crypto.getRandomValues(u8);
    } else {
      for (let i = 0; i < u8.length; i++) u8[i] = Math.floor(Math.random() * 256);
    }
    return buf;
  }
};

// Buffer shim: use Uint8Array but provide Buffer.concat and compare helpers used by tests
export class BufferShim extends Uint8Array {
  static allocUnsafe(len) { return new BufferShim(len); }
  static from(u8) { return new BufferShim(u8); }
  static concat(list) {
    const total = list.reduce((s, b) => s + b.length, 0);
    const out = new BufferShim(total);
    let off = 0;
    for (const b of list) { out.set(b, off); off += b.length; }
    return out;
  }
  static compare(a, b) {
    if (a.length !== b.length) return a.length - b.length;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return a[i] - b[i];
    return 0;
  }
}

export const Buffer = BufferShim;

// Minimal process shim
export const process = {
  argv: ['deno', ...Deno.args],
  env: Deno.env ? Deno.env.toObject() : {},
  exit: (code=0) => Deno.exit(code),
  // hrtime.bigint approximation using performance.now()
  hrtime: {
    bigint: () => BigInt(Math.floor(performance.now() * 1e6))
  },
  memoryUsage: () => {
    if (typeof Deno.memoryUsage === 'function') return Deno.memoryUsage();
    // best-effort: provide rss if available, else zeros
    try {
      // Deno.metrics() is available in some versions
      if (typeof Deno.memoryUsage === 'function') return Deno.memoryUsage();
    } catch (e) {}
    return { rss: 0, heapTotal: 0, heapUsed: 0, external: 0, arrayBuffers: 0 };
  }
};

// child_process shim: spawnSync using Deno.Command (sync) where available
export const child_process = {
  spawnSync: (cmd, args, opts) => {
    // normalize args
    const argv = Array.isArray(args) ? args : [];
    // prefer Deno.Command.outputSync when available
    try {
      if (typeof Deno.Command === 'function') {
        const c = new Deno.Command(cmd, { args: argv, stderr: 'piped', stdout: 'piped' });
        const out = c.outputSync();
        return { status: out.status.code, stdout: out.stdout, stderr: out.stderr };
      }
    } catch (e) {
      // fallback to Deno.run async pattern (best-effort)
    }
    // fallback: try Deno.run and wait for completion synchronously via async-to-sync trick
    try {
      const p = Deno.run({ cmd: [cmd, ...argv], stdout: 'piped', stderr: 'piped' });
      const out = p.outputSync ? p.outputSync() : new Uint8Array();
      const status = p.statusSync ? p.statusSync() : { code: 0 };
      return { status: status.code, stdout: out, stderr: new Uint8Array() };
    } catch (e) {
      return { status: 1, stdout: new Uint8Array(), stderr: new TextEncoder().encode(String(e)) };
    }
  }
};

// zlib shim: prefer Node via spawning `node -e` helper; returns Buffer (Uint8Array)
export const zlib = {
  deflateSync: (buf) => {
    try {
      const script = `const zlib=require('zlib'); const b=Buffer.from(process.argv[1],'base64'); const out=zlib.deflateSync(b); process.stdout.write(out);`;
      const base = Buffer.from(buf).toString('base64');
      const c = new Deno.Command('node', { args: ['-e', script, base], stdout: 'piped' });
      const out = c.outputSync();
      return new Uint8Array(out.stdout);
    } catch (e) {
      throw new Error('zlib.deflateSync unavailable: ' + e);
    }
  },
  gzipSync: (buf) => {
    try {
      const script = `const zlib=require('zlib'); const b=Buffer.from(process.argv[1],'base64'); const out=zlib.gzipSync(b); process.stdout.write(out);`;
      const base = Buffer.from(buf).toString('base64');
      const c = new Deno.Command('node', { args: ['-e', script, base], stdout: 'piped' });
      const out = c.outputSync();
      return new Uint8Array(out.stdout);
    } catch (e) {
      throw new Error('zlib.gzipSync unavailable: ' + e);
    }
  }
};
