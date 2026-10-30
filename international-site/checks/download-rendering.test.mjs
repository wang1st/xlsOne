import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

test('enhancing static downloads replaces rather than duplicates the published cards', async () => {
  const context = { window: {} };
  vm.runInNewContext(readFileSync(new URL('../config.js', import.meta.url), 'utf8'), context);
  const downloads = context.window.XLSONE_CONFIG.downloads;
  const list = { children: downloads.map(() => ({})), replaceChildren() { this.children = []; }, append(child) { this.children.push(child); } };
  const pending = { hidden: true };
  const originals = Object.fromEntries(['window', 'document'].map(name => [name, globalThis[name]]));
  globalThis.window = context.window;
  globalThis.document = {
    getElementById: id => id === 'download-list' ? list : id === 'download-pending' ? pending : null,
    querySelectorAll: () => [],
    createElement: () => ({ children: [], append(...children) { this.children.push(...children); } })
  };
  try {
    await import('../main.js?static-download-enhancement');
    assert.equal(list.children.length, downloads.length);
    assert.equal(pending.hidden, true);
    for (const [i, card] of list.children.entries()) {
      assert.equal(card.children[2].href, downloads[i].url);
      assert.equal(card.children[4].textContent, downloads[i].sha256);
    }
  } finally {
    for (const [name, value] of Object.entries(originals)) {
      if (value === undefined) delete globalThis[name]; else globalThis[name] = value;
    }
  }
});
