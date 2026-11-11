(function () {
  'use strict';

  const EJUDGE_TO_PRISM = {
    'gcc': 'c',
    'g++': 'cpp',
    'javac': 'java',
    'mcs': 'csharp',
    'php': 'php',
    'python': 'python',
    'python3': 'python',
    'gccgo': 'go',
    'node': 'javascript',
    'perl': 'perl',
    'basic': 'basic',
    'pascal': 'pascal',
    'pasabc-linux': 'pascal',
    'objectivec': 'objectivec',
    'ruby': 'ruby',
    'rust': 'rust',
    'kotlin': 'kotlin'
  };

  const STATUS_FILTER = '(status == OK || status == PR || status == RJ)';
  const LONG_BLOCK_LINES = 30;

  function trimmedText(element) {
    return element ? element.textContent.trim() : '';
  }

  function headerTexts(table) {
    if (!table.rows || !table.rows[0]) return [];
    return Array.from(table.rows[0].cells).map(function (cell) {
      return cell.textContent.replace(/\u00a0/g, ' ').replace(/\s+/g, ' ').trim();
    });
  }

  function headerMatches(header, name) {
    if (header === name) return true;
    return header.startsWith(name + ' ');
  }

  function columnIndex(headers, names) {
    for (const name of names) {
      const index = headers.findIndex(function (header) {
        return headerMatches(header, name);
      });
      if (index >= 0) return index;
    }
    return -1;
  }

  function cellByHeader(row, headers, names) {
    const index = columnIndex(headers, names);
    if (index < 0 || !row.cells[index]) return '';
    return row.cells[index].textContent.trim();
  }

  function rowRunId(row, sourceColumn) {
    if (sourceColumn >= 0 && row.cells[sourceColumn]) {
      const link = row.cells[sourceColumn].querySelector('a[href]');
      if (link) {
        const runId = new URL(link.href, window.location.href).searchParams.get('run_id');
        if (runId && /^\d+$/.test(runId)) return Number(runId);
      }
    }
    const marker = row.querySelector('[id^="ej_dd_"]');
    if (marker) {
      const match = marker.id.match(/^ej_dd_(\d+)$/);
      if (match) return Number(match[1]);
    }
    return null;
  }

  function sourceUrl(row, sourceColumn) {
    if (sourceColumn < 0 || !row.cells[sourceColumn]) return null;
    const link = row.cells[sourceColumn].querySelector('a[href]');
    return link ? link.href : null;
  }

  function rowAuthorFilter(row, headers) {
    const userId = cellByHeader(row, headers, ['User ID', 'User id', 'UID']);
    if (/^\d+$/.test(userId)) return 'uid == ' + userId;
    const login = cellByHeader(row, headers, ['Login', 'User login']);
    if (login) return 'login == ' + JSON.stringify(login);
    const name = cellByHeader(row, headers, ['User name', 'User', 'Name']);
    if (name) return 'name == ' + JSON.stringify(name);
    return null;
  }

  function problemCode(value) {
    return String(value || '')
      .trim()
      .split(/\s+-\s+/)[0]
      .replace(/-\d+$/, '')
      .replace(/-\?$/, '');
  }

  function rowLanguage(row, headers) {
    const direct = cellByHeader(row, headers, ['Language', 'Lang']);
    if (direct) return direct;
    for (const cell of row.cells) {
      const value = cell.textContent.trim();
      if (EJUDGE_TO_PRISM[value]) return value;
    }
    return '';
  }

  function isRunsTable(table) {
    if (!table.rows || table.rows.length < 2) return false;
    const headers = headerTexts(table);
    if (columnIndex(headers, ['Source']) >= 0 && columnIndex(headers, ['Report']) >= 0) {
      return !!table.querySelector('a[href*="run_id="]');
    }
    return false;
  }

  function findRunsTable() {
    let best = null;
    let bestRows = 0;
    for (const table of document.querySelectorAll('table')) {
      if (!isRunsTable(table)) continue;
      if (table.rows.length > bestRows) {
        best = table;
        bestRows = table.rows.length;
      }
    }
    if (best) return best;
    for (const table of document.querySelectorAll('table')) {
      if (!table.rows || table.rows.length < 2) continue;
      if (!table.querySelector('a[href*="run_id="]')) continue;
      if (table.rows.length > bestRows) {
        best = table;
        bestRows = table.rows.length;
      }
    }
    return best;
  }

  function findRunsTableInDocument(doc) {
    let best = null;
    let bestRows = 0;
    for (const table of doc.querySelectorAll('table')) {
      if (!isRunsTable(table)) continue;
      if (table.rows.length > bestRows) {
        best = table;
        bestRows = table.rows.length;
      }
    }
    return best;
  }

  function getSessionId() {
    const fromUrl = new URL(window.location.href).searchParams.get('SID');
    if (fromUrl) return fromUrl;
    const hidden = document.querySelector('input[name="SID"]');
    if (hidden && hidden.value) return hidden.value;
    for (const link of document.querySelectorAll('a[href*="SID="]')) {
      const value = new URL(link.href, window.location.href).searchParams.get('SID');
      if (value) return value;
    }
    return null;
  }

  function roleEndpoint() {
    const url = new URL(window.location.href);
    const segments = url.pathname.split('/');
    const roles = ['master', 'new-master', 'judge'];
    let index = -1;
    for (let i = segments.length - 1; i >= 0; --i) {
      if (roles.indexOf(segments[i]) >= 0) {
        index = i;
        break;
      }
    }
    if (index >= 0) url.pathname = segments.slice(0, index + 1).join('/');
    url.search = '';
    url.hash = '';
    return url;
  }

  function getContestId() {
    const url = new URL(window.location.href);
    const fromQuery = url.searchParams.get('contest_id');
    if (fromQuery) return fromQuery;
    const match = url.pathname.match(/\/C(\d+)(?:\/|$)/);
    return match ? match[1] : null;
  }

  function makeUrl(parameters) {
    const url = roleEndpoint();
    const contestId = getContestId();
    if (contestId) url.searchParams.set('contest_id', contestId);
    const localeId = new URL(window.location.href).searchParams.get('locale_id');
    if (localeId) url.searchParams.set('locale_id', localeId);
    const sessionId = getSessionId();
    if (sessionId) url.searchParams.set('SID', sessionId);
    for (const [name, value] of Object.entries(parameters || {})) {
      url.searchParams.set(name, value);
    }
    return url;
  }

  function makeMasterUrl(filterExpression) {
    const url = makeUrl();
    url.searchParams.set('filter_expr', filterExpression);
    return url;
  }

  function isLoginPage(text) {
    return /<title>[^<]*login page<\/title>/i.test(text) ||
      (/<input[^>]+name=["']login["']/i.test(text) &&
        /<input[^>]+name=["']password["']/i.test(text));
  }

  let runsFilterQueue = Promise.resolve();

  function fetchUserRunsPage(filterExpression) {
    const task = runsFilterQueue.then(function () {
      return fetchUserRunsPageSerial(filterExpression);
    });
    runsFilterQueue = task.catch(function () {});
    return task;
  }

  async function fetchUserRunsPageSerial(filterExpression) {
    const filterInput = document.querySelector('input[name="filter_expr"]');
    const firstInput = document.querySelector('[name="filter_first_run"], [name="first_run"]');
    const lastInput = document.querySelector('[name="filter_last_run"], [name="last_run"]');
    const params = new URL(window.location.href).searchParams;
    const savedFilter = filterInput ? filterInput.value : null;
    const savedFirst = firstInput ? firstInput.value : (params.get('filter_first_run') ?? params.get('first_run') ?? '');
    const savedLast = lastInput ? lastInput.value : (params.get('filter_last_run') ?? params.get('last_run') ?? '');
    if (savedFilter === null) {
      return {document: null, reason: 'не удалось сохранить исходный фильтр'};
    }
    const url = makeMasterUrl(filterExpression);
    url.searchParams.set('filter_first_run', '0');
    url.searchParams.set('filter_last_run', '-1');
    url.searchParams.set('filter_view', '1');
    try {
      const response = await fetch(url.href, {credentials: 'same-origin'});
      if (!response.ok) return {document: null, reason: 'сервер вернул HTTP ' + response.status};
      const page = await response.text();
      if (isLoginPage(page)) return {document: null, reason: 'ejudge запросил повторный вход'};
      const documentPage = new DOMParser().parseFromString(page, 'text/html');
      if (!findRunsTableInDocument(documentPage)) {
        return {document: null, reason: 'ejudge не вернул таблицу посылок'};
      }
      return {document: documentPage, reason: null};
    } finally {
      const resetUrl = makeUrl({action: 'reset-filter'});
      const restoreUrl = makeMasterUrl(savedFilter);
      if (savedFirst !== '') restoreUrl.searchParams.set('filter_first_run', savedFirst);
      if (savedLast !== '') restoreUrl.searchParams.set('filter_last_run', savedLast);
      restoreUrl.searchParams.set('filter_view', '1');
      try {
        const resetResponse = await fetch(resetUrl.href, {credentials: 'same-origin'});
        if (!resetResponse.ok || isLoginPage(await resetResponse.text())) {
          throw new Error('сброс фильтра не удался');
        }
        const response = await fetch(restoreUrl.href, {credentials: 'same-origin'});
        if (!response.ok || isLoginPage(await response.text())) {
          throw new Error('HTTP ' + response.status);
        }
      } catch (error) {
        throw new Error('Не удалось восстановить фильтр: ' + (error && error.message || error));
      }
    }
  }

  function fetchRunPage(url) {
    return fetch(url, {credentials: 'same-origin'}).then(async function (response) {
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const page = await response.text();
      if (isLoginPage(page)) throw new Error('ejudge запросил повторный вход');
      return new DOMParser().parseFromString(page, 'text/html');
    });
  }

  function extractSource(doc) {
    const projection = doc.getElementById('prog');
    if (!projection) return {kind: 'none'};
    const image = projection.querySelector('img[src]');
    if (image) return {kind: 'image', src: image.src};
    const table = projection.querySelector('table');
    if (table && table.rows[0] && table.rows[0].cells.length >= 2) {
      const nodes = table.rows[0].cells[1].querySelectorAll('span > tt');
      if (nodes.length > 0) {
        const lines = Array.from(nodes).map(function (node) {
          return node.textContent.replace(/\u00a0/g, ' ');
        });
        return {kind: 'text', code: lines.join('\n')};
      }
    }
    const text = projection.textContent.replace(/\u00a0/g, ' ').trim();
    return text ? {kind: 'text', code: text} : {kind: 'none'};
  }

  function textWithoutControls(element) {
    const copy = element.cloneNode(true);
    copy.querySelectorAll('button, input, select, textarea, script, style').forEach(function (node) {
      node.remove();
    });
    copy.querySelectorAll('br').forEach(function (br) {
      br.replaceWith('\n');
    });
    return copy.textContent
      .replace(/\u00a0/g, ' ')
      .replace(/[ \t]+\n/g, '\n')
      .replace(/\n[ \t]+/g, '\n')
      .replace(/\n{3,}/g, '\n\n')
      .trim();
  }

  function extractReviewComments(doc) {
    const comments = [];
    for (const button of doc.querySelectorAll('input[type="button"], button')) {
      const match = (button.getAttribute('onclick') || '').match(/addReviewMsg\((\d+)\)/);
      if (!match) continue;
      const span = doc.getElementById('ReviewMsg' + match[1]);
      if (!span) continue;
      const text = span.textContent.trim();
      if (text) comments.push(text);
    }
    return [...new Set(comments)];
  }

  function extractRunComments(doc) {
    const comments = [];
    for (const table of doc.querySelectorAll('table.message-table')) {
      if (!table.rows || !table.rows[0]) continue;
      const headers = Array.from(table.rows[0].cells).map(function (cell) {
        return cell.textContent.trim();
      });
      const commentColumn = headers.indexOf('Run comment');
      if (commentColumn < 0) continue;
      const authorColumn = headers.indexOf('Author');
      for (const row of Array.from(table.rows).slice(1)) {
        if (row.cells.length <= commentColumn) continue;
        const text = textWithoutControls(row.cells[commentColumn]);
        if (!text) continue;
        const author = authorColumn >= 0 && row.cells[authorColumn]
          ? textWithoutControls(row.cells[authorColumn]).split('\n')[0]
          : '';
        comments.push({author: author, text: text});
      }
    }
    const seen = new Set();
    return comments.filter(function (comment) {
      const key = comment.author + '\n' + comment.text;
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    });
  }

  function appendComment(commentArea, text) {
    const prefix = commentArea.value.trim().length === 0 ? '' : '\n';
    commentArea.value += prefix + text;
    commentArea.focus();
    commentArea.dispatchEvent(new Event('input', {bubbles: true}));
  }

  function renderComments(doc, cell, commentArea) {
    const reviews = extractReviewComments(doc);
    const history = extractRunComments(doc);
    if (reviews.length === 0 && history.length === 0) {
      if (cell.parentElement) cell.parentElement.remove();
      return;
    }
    cell.textContent = '';
    if (reviews.length > 0) {
      const title = document.createElement('strong');
      title.textContent = 'Встроенные комментарии:';
      cell.appendChild(title);
      for (const text of reviews) {
        const line = document.createElement('div');
        line.className = 'fe-comment-item';
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = '(+)';
        button.title = 'Добавить этот комментарий в поле review';
        button.addEventListener('click', function () {
          appendComment(commentArea, text);
        });
        line.appendChild(button);
        const label = document.createElement('span');
        label.textContent = ' ' + text.replace(/\s*\n\s*/g, ' ') + ' ';
        line.appendChild(label);
        cell.appendChild(line);
      }
    }
    if (history.length > 0) {
      const title = document.createElement('strong');
      title.textContent = 'Ранее полученные комментарии:';
      title.className = 'fe-comment-history-title';
      cell.appendChild(title);
      for (const comment of history) {
        const item = document.createElement('div');
        item.className = 'fe-comment-history-item';
        if (comment.author) {
          const author = document.createElement('strong');
          author.textContent = comment.author + ': ';
          item.appendChild(author);
        }
        item.appendChild(document.createTextNode(comment.text));
        cell.appendChild(item);
      }
    }
  }

  function prismLanguage(name) {
    const language = EJUDGE_TO_PRISM[name];
    if (!language || !window.Prism || !window.Prism.languages[language]) return null;
    return language;
  }

  function highlightedLines(code, language) {
    const lang = prismLanguage(language);
    if (!lang) return null;
    let html;
    try {
      html = window.Prism.highlight(code, window.Prism.languages[lang], lang);
    } catch (error) {
      return null;
    }
    const parsed = new DOMParser().parseFromString('<div>' + html + '</div>', 'text/html');
    const root = parsed.body.firstChild;
    const lines = [document.createDocumentFragment()];
    function visit(node, classes) {
      if (node.nodeType === 3) {
        const parts = node.nodeValue.split('\n');
        parts.forEach(function (part, index) {
          if (index > 0) lines.push(document.createDocumentFragment());
          if (!part) return;
          const target = lines[lines.length - 1];
          if (classes.length === 0) {
            target.appendChild(document.createTextNode(part));
            return;
          }
          const span = document.createElement('span');
          span.className = Array.from(new Set(classes)).join(' ');
          span.appendChild(document.createTextNode(part));
          target.appendChild(span);
        });
      } else if (node.nodeType === 1) {
        const next = node.className
          ? classes.concat(node.className.split(/\s+/).filter(Boolean))
          : classes;
        for (const child of node.childNodes) visit(child, next);
      }
    }
    for (const child of root.childNodes) visit(child, []);
    return lines;
  }

  function renderFullSolution(cell, code, language, note) {
    cell.textContent = '';
    if (note) {
      const notice = document.createElement('div');
      notice.className = 'fe-note';
      notice.textContent = note;
      cell.appendChild(notice);
    }
    const pre = document.createElement('pre');
    pre.className = 'fe-source';
    const lang = prismLanguage(language);
    if (lang) {
      pre.classList.add('language-' + lang);
      pre.innerHTML = window.Prism.highlight(code, window.Prism.languages[lang], lang);
    } else {
      pre.textContent = code;
    }
    cell.appendChild(pre);
  }

  function renderImage(cell, src) {
    cell.textContent = '';
    const image = document.createElement('img');
    image.className = 'fe-image';
    image.src = src;
    cell.appendChild(image);
  }

  function makeLineDiff(oldLines, newLines, maxEdits) {
    const max = Math.min(oldLines.length + newLines.length, maxEdits || 1500);
    let previous = new Map([[1, 0]]);
    const trace = [];
    for (let distance = 0; distance <= max; distance++) {
      const current = new Map();
      for (let diagonal = -distance; diagonal <= distance; diagonal += 2) {
        let x;
        if (diagonal === -distance ||
            (diagonal !== distance &&
              (previous.get(diagonal - 1) ?? -Infinity) < (previous.get(diagonal + 1) ?? -Infinity))) {
          x = previous.get(diagonal + 1) ?? 0;
        } else {
          x = (previous.get(diagonal - 1) ?? 0) + 1;
        }
        let y = x - diagonal;
        while (x < oldLines.length && y < newLines.length && oldLines[x] === newLines[y]) {
          x++;
          y++;
        }
        current.set(diagonal, x);
        if (x >= oldLines.length && y >= newLines.length) {
          trace.push(current);
          return backtrackLineDiff(trace, oldLines, newLines);
        }
      }
      trace.push(current);
      previous = current;
    }
    return null;
  }

  function backtrackLineDiff(trace, oldLines, newLines) {
    let x = oldLines.length;
    let y = newLines.length;
    const result = [];
    for (let distance = trace.length - 1; distance > 0; distance--) {
      const previous = trace[distance - 1];
      const diagonal = x - y;
      const previousDiagonal = diagonal === -distance ||
        (diagonal !== distance &&
          (previous.get(diagonal - 1) ?? -Infinity) < (previous.get(diagonal + 1) ?? -Infinity))
        ? diagonal + 1
        : diagonal - 1;
      const previousX = previous.get(previousDiagonal) ?? 0;
      const previousY = previousX - previousDiagonal;
      while (x > previousX && y > previousY) {
        result.push({kind: 'same', text: oldLines[--x]});
        y--;
      }
      if (x === previousX) {
        result.push({kind: 'add', text: newLines[--y]});
      } else {
        result.push({kind: 'remove', text: oldLines[--x]});
      }
    }
    while (x > 0 && y > 0) {
      result.push({kind: 'same', text: oldLines[--x]});
      y--;
    }
    while (x > 0) result.push({kind: 'remove', text: oldLines[--x]});
    while (y > 0) result.push({kind: 'add', text: newLines[--y]});
    return result.reverse();
  }

  function splitLines(code) {
    return code.replace(/\r\n?/g, '\n').split('\n');
  }

  function indentationOf(line) {
    return line.match(/^\s*/)[0].length;
  }

  function sanitizeCppLine(line, state) {
    let result = '';
    for (let index = 0; index < line.length; index++) {
      const char = line[index];
      const next = line[index + 1];
      if (state.blockComment) {
        if (char === '*' && next === '/') {
          state.blockComment = false;
          result += '  ';
          index++;
        } else {
          result += ' ';
        }
        continue;
      }
      if (state.string) {
        if (char === '\\') {
          result += '  ';
          index++;
          continue;
        }
        if (char === state.string) state.string = null;
        result += ' ';
        continue;
      }
      if (char === '/' && next === '/') {
        result += ' '.repeat(line.length - index);
        break;
      }
      if (char === '/' && next === '*') {
        state.blockComment = true;
        result += '  ';
        index++;
        continue;
      }
      if (char === '"' || char === "'") {
        state.string = char;
        result += ' ';
        continue;
      }
      result += char;
    }
    return result;
  }

  function cppBlockLength(lines, start) {
    let depth = 0;
    let openLine = -1;
    const state = {blockComment: false, string: null};
    for (let index = start; index < lines.length; index++) {
      const code = sanitizeCppLine(lines[index], state);
      if (openLine < 0) {
        const open = code.indexOf('{');
        const semicolon = code.indexOf(';');
        if (open < 0) {
          if (semicolon >= 0) return null;
          continue;
        }
        if (semicolon >= 0 && semicolon < open) return null;
        openLine = index;
      }
      for (const char of code) {
        if (char === '{') depth++;
        else if (char === '}') depth--;
        if (depth === 0 && char === '}') return index - start + 1;
      }
    }
    return lines.length - start;
  }

  function hasCppInheritanceContinuation(lines, start) {
    for (let index = start + 1; index < lines.length; index++) {
      const line = lines[index].trim();
      if (!line) continue;
      return /^(?:public|private|protected)\b/.test(line);
    }
    return false;
  }

  function hasLongClass(code) {
    const lines = splitLines(code);
    for (let start = 0; start < lines.length; start++) {
      const line = lines[start];
      const pythonClass = line.match(/^(\s*)class\s+[A-Za-z_]\w*(?:\s*\([^)]*\))?\s*:\s*(?:#.*)?$/);
      if (pythonClass && !hasCppInheritanceContinuation(lines, start)) {
        const indentation = pythonClass[1].length;
        let end = start + 1;
        while (end < lines.length) {
          const current = lines[end];
          if (current.trim().length > 0 && indentationOf(current) <= indentation) break;
          end++;
        }
        if (end - start > LONG_BLOCK_LINES) return true;
        continue;
      }
      const cpp = /^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+[A-Za-z_]\w*/.test(line);
      const template = /^\s*template\s*</.test(line);
      if (!cpp && !template) continue;
      const length = cppBlockLength(lines, start);
      if (length !== null && length > LONG_BLOCK_LINES) return true;
    }
    return false;
  }

  function renderDiffLine(body, prefix, text, className) {
    const line = document.createElement('div');
    line.className = 'fe-line ' + className;
    line.textContent = prefix + text;
    body.appendChild(line);
  }

  function renderDiff(cell, diff, previous, language, oldCode, newCode) {
    cell.textContent = '';
    cell.classList.add('fe-diff-cell');
    const title = document.createElement('div');
    title.className = 'fe-note';
    title.textContent = 'Изменения относительно посылки ' + previous.runId +
      ' по задаче ' + previous.problem + ':';
    cell.appendChild(title);
    const scroll = document.createElement('div');
    scroll.className = 'fe-diff-scroll';
    const body = document.createElement('div');
    body.className = 'fe-diff';
    scroll.appendChild(body);
    cell.appendChild(scroll);
    const visible = new Set();
    diff.forEach(function (entry, index) {
      if (entry.kind === 'same') return;
      for (let context = Math.max(0, index - 2); context <= Math.min(diff.length - 1, index + 2); context++) {
        visible.add(context);
      }
    });
    let oldHighlighted = null;
    let newHighlighted = null;
    try {
      oldHighlighted = highlightedLines(oldCode, language);
      newHighlighted = highlightedLines(newCode, language);
    } catch (error) {
      oldHighlighted = null;
      newHighlighted = null;
    }
    let oldIndex = 0;
    let newIndex = 0;
    let skipped = 0;
    diff.forEach(function (entry, index) {
      const oldLine = entry.kind !== 'add' ? oldIndex++ : -1;
      const newLine = entry.kind !== 'remove' ? newIndex++ : -1;
      if (!visible.has(index)) {
        skipped++;
        return;
      }
      if (skipped > 0) {
        renderDiffLine(body, '… ', skipped + ' неизменённых строк', 'skip');
        skipped = 0;
      }
      const isAdd = entry.kind === 'add';
      const isRemove = entry.kind === 'remove';
      const line = document.createElement('div');
      line.className = 'fe-line ' + (isAdd ? 'add' : isRemove ? 'del' : 'same');
      line.appendChild(document.createTextNode(isAdd ? '+ ' : isRemove ? '- ' : '  '));
      const highlighted = isRemove
        ? oldHighlighted && oldHighlighted[oldLine]
        : newHighlighted && newHighlighted[newLine];
      line.appendChild(highlighted || document.createTextNode(entry.text));
      body.appendChild(line);
    });
    if (skipped > 0) {
      renderDiffLine(body, '… ', skipped + ' неизменённых строк', 'skip');
    }
  }

  function previousProblemCode(code) {
    const select = document.querySelector('select[name="problem"]');
    if (!select) return null;
    const codes = Array.from(select.options)
      .map(function (option) {
        return option.textContent.split(/\s+-\s+/)[0].trim();
      })
      .filter(Boolean);
    let index = codes.indexOf(code);
    if (index < 0) {
      index = codes.findIndex(function (candidate) {
        return candidate.startsWith(code) || code.startsWith(candidate);
      });
    }
    return index > 0 ? codes[index - 1] : null;
  }

  function findPreviousSuccessfulRun(doc, problem, currentRunId, authorExpression) {
    const table = findRunsTableInDocument(doc);
    if (!table) return null;
    const headers = headerTexts(table);
    const sourceColumn = columnIndex(headers, ['Source']);
    if (sourceColumn < 0) return null;
    let best = null;
    for (const row of Array.from(table.rows).slice(1)) {
      const runId = rowRunId(row, sourceColumn);
      if (runId === null || runId >= currentRunId) continue;
      if (problemCode(cellByHeader(row, headers, ['Problem', 'Prob'])) !== problem) continue;
      if (authorExpression && rowAuthorFilter(row, headers) !== authorExpression) continue;
      const url = sourceUrl(row, sourceColumn);
      if (!url) continue;
      if (!best || runId > best.runId) {
        best = {runId: runId, sourceUrl: url, language: rowLanguage(row, headers)};
      }
    }
    return best;
  }

  function makeHistoryGroups(runs) {
    const byAuthor = new Map();
    const byProblem = new Map();
    for (const run of runs) {
      const author = run.authorFilter;
      const problem = previousProblemCode(run.problemCode);
      if (!author || !problem) continue;
      if (!byAuthor.has(author)) byAuthor.set(author, []);
      if (!byProblem.has(problem)) byProblem.set(problem, []);
      byAuthor.get(author).push(run);
      byProblem.get(problem).push(run);
    }
    const useProblem = byProblem.size <= byAuthor.size;
    const groups = useProblem ? byProblem : byAuthor;
    const sourceCache = new Map();
    for (const [key, members] of groups) {
      let pending = null;
      const filter = (useProblem ? 'prob == ' + JSON.stringify(key) : '(' + key + ')') +
        ' && ' + STATUS_FILTER;
      const group = {
        sourceCache: sourceCache,
        getPage: function () {
          if (!pending) {
            pending = fetchUserRunsPage(filter);
          }
          return pending;
        }
      };
      for (const member of members) member.group = group;
    }
  }

  async function getPreviousSolution(run) {
    const problem = previousProblemCode(run.problemCode);
    if (!problem) {
      run.previousReason = 'для этой задачи нет предыдущей';
      return null;
    }
    if (!run.authorFilter) {
      run.previousReason = 'в строке посылки не найден автор';
      return null;
    }
    if (!run.group) {
      run.previousReason = 'не удалось подготовить поиск истории';
      return null;
    }
    let page;
    try {
      page = await run.group.getPage();
    } catch (error) {
      run.previousReason = error && error.message || String(error);
      return null;
    }
    if (!page.document) {
      run.previousReason = page.reason || 'не удалось загрузить историю';
      return null;
    }
    const previous = findPreviousSuccessfulRun(page.document, problem, run.runId, run.authorFilter);
    if (!previous) {
      run.previousReason = 'до этой посылки по задаче ' + problem + ' нет OK, PR или RJ';
      return null;
    }
    if (!run.group.sourceCache.has(previous.runId)) {
      run.group.sourceCache.set(previous.runId, fetchRunPage(previous.sourceUrl).then(function (doc) {
        const source = extractSource(doc);
        if (source.kind !== 'text') throw new Error('исходник посылки ' + previous.runId + ' недоступен');
        return source.code;
      }));
    }
    try {
      const code = await run.group.sourceCache.get(previous.runId);
      return {code: code, runId: previous.runId, problem: problem};
    } catch (error) {
      run.group.sourceCache.delete(previous.runId);
      run.previousReason = error && error.message || String(error);
      return null;
    }
  }

  async function findDiff(run, state) {
    try {
      const previous = await getPreviousSolution(run);
      if (!previous) return {previous: null, diff: null};
      return {
        previous: previous,
        diff: makeLineDiff(splitLines(previous.code), splitLines(state.code))
      };
    } catch (error) {
      run.previousReason = error && error.message || String(error);
      return {previous: null, diff: null};
    }
  }

  function diffUnavailableNote(run, result) {
    if (result.previous) return 'Разность слишком большая.';
    return 'Разность не показана: ' + run.previousReason + '.';
  }

  function renderSolution(cell, run, state, mode) {
    cell.textContent = '';
    cell.classList.toggle('fe-diff-cell', mode === 'diff');
    if (run.modeCell) {
      run.modeCell.textContent = '';
      if (state.diffAvailable !== false) {
        const toggle = document.createElement('button');
        toggle.type = 'button';
        toggle.className = 'fe-mode-toggle';
        toggle.textContent = mode === 'diff' ? 'Source' : 'Diff';
        toggle.addEventListener('click', function () {
          if (mode === 'diff') {
            renderSolution(cell, run, state, 'full');
          } else {
            showDiff(cell, run, state);
          }
        });
        run.modeCell.appendChild(toggle);
      }
    }
    const content = document.createElement('div');
    content.className = 'fe-solution-content';
    cell.appendChild(content);
    if (mode === 'diff') {
      renderDiff(content, state.diff, state.previous, run.language, state.previous.code, state.code);
    } else {
      renderFullSolution(content, state.code, run.language, state.fullNote);
    }
  }

  async function showDiff(cell, run, state) {
    if (state.diff) {
      renderSolution(cell, run, state, 'diff');
      return;
    }
    if (state.diffLoading) return;
    state.diffLoading = true;
    const content = cell.querySelector('.fe-solution-content');
    if (content) content.textContent = 'Загрузка diff…';
    const toggle = run.modeCell && run.modeCell.querySelector('.fe-mode-toggle');
    if (toggle) toggle.disabled = true;
    const result = await findDiff(run, state);
    state.diffLoading = false;
    if (result.diff) {
      state.previous = result.previous;
      state.diff = result.diff;
      state.diffAvailable = true;
      renderSolution(cell, run, state, 'diff');
      return;
    }
    state.diffAvailable = false;
    state.fullNote = diffUnavailableNote(run, result);
    renderSolution(cell, run, state, 'full');
  }

  async function processSolution(cell, run) {
    cell.textContent = 'Загрузка решения…';
    try {
      const doc = await fetchRunPage(run.sourceUrl);
      renderComments(doc, run.commentsCell, run.commentArea);
      const source = extractSource(doc);
      if (source.kind === 'image') {
        renderImage(cell, source.src);
        return;
      }
      if (source.kind !== 'text') {
        cell.textContent = 'Исходный текст недоступен';
        return;
      }
      const state = {
        code: source.code,
        previous: null,
        diff: null,
        diffAvailable: null,
        diffLoading: false,
        fullNote: ''
      };
      run.solutionState = state;
      if (!hasLongClass(source.code)) {
        renderSolution(cell, run, state, 'full');
        return;
      }
      cell.textContent = 'Загрузка решения…';
      const result = await findDiff(run, state);
      if (result.diff) {
        state.previous = result.previous;
        state.diff = result.diff;
        state.diffAvailable = true;
        renderSolution(cell, run, state, 'diff');
      } else {
        state.diffAvailable = false;
        state.fullNote = diffUnavailableNote(run, result);
        renderSolution(cell, run, state, 'full');
      }
    } catch (error) {
      cell.textContent = String(error && error.message || error);
    }
  }

  async function postAction(runId, action, parameters) {
    const body = new URLSearchParams();
    const sessionId = getSessionId();
    if (sessionId) body.set('SID', sessionId);
    body.set('run_id', String(runId));
    body.set('action', action);
    for (const [name, value] of Object.entries(parameters || {})) {
      body.set(name, value);
    }
    const response = await fetch(makeUrl().href, {
      method: 'POST',
      credentials: 'same-origin',
      headers: {'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'},
      body: body
    });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    return response;
  }

  function createButtons(run, commentArea, cell) {
    const definitions = [
      {label: 'Just OK', action: 'priv-submit-run-just-ok'},
      {label: 'Comment&OK', action: 'priv-submit-run-comment-and-ok', comment: true},
      {label: 'Reject', action: 'priv-submit-run-comment-and-reject', comment: true},
      {label: 'DQ', action: 'change-status', extra: {status: '10'}},
      {label: 'Just SUMMON', action: 'priv-submit-run-just-summon'}
    ];
    for (const definition of definitions) {
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = definition.label;
      button.addEventListener('click', async function () {
        for (const other of cell.querySelectorAll('button')) other.style.background = '';
        button.style.background = '#FFFACD';
        try {
          let message;
          if (definition.comment) {
            message = commentArea.value;
            if (!message || !message.trim()) throw new Error('Нет сообщения');
          }
          const parameters = Object.assign({}, definition.extra || {});
          if (message != null) parameters.msg_text = message;
          await postAction(run.runId, definition.action, parameters);
          button.style.background = '#4CAF50';
        } catch (error) {
          button.style.background = '#f44336';
          button.title = String(error && error.message || error);
        }
      });
      cell.appendChild(button);
    }
  }

  function parseRuns(table) {
    const headers = headerTexts(table);
    const sourceColumn = columnIndex(headers, ['Source']);
    const runs = [];
    for (const row of Array.from(table.rows).slice(1)) {
      const runId = rowRunId(row, sourceColumn);
      const url = sourceUrl(row, sourceColumn);
      if (runId === null || !url) continue;
      runs.push({
        row: row,
        runId: runId,
        sourceUrl: url,
        problemCode: problemCode(cellByHeader(row, headers, ['Problem', 'Prob'])),
        language: rowLanguage(row, headers),
        authorFilter: rowAuthorFilter(row, headers),
        previousReason: ''
      });
    }
    return runs;
  }

  function makeSolutionTable(table) {
    const runs = parseRuns(table);
    makeHistoryGroups(runs);
    const solutions = document.createElement('table');
    solutions.className = 'fe-solutions';
    for (const run of runs) {
      const headRow = document.createElement('tr');
      headRow.className = 'fe-run-row';
      const headCell = document.createElement('td');
      headCell.colSpan = 3;
      const nested = document.createElement('table');
      nested.className = 'fe-run-copy';
      const copy = run.row.cloneNode(true);
      copy.querySelectorAll('[id]').forEach(function (node) {
        node.removeAttribute('id');
      });
      copy.querySelectorAll('a[href^="javascript:"]').forEach(function (link) {
        link.removeAttribute('href');
      });
      nested.appendChild(copy);
      headCell.appendChild(nested);
      headRow.appendChild(headCell);
      solutions.appendChild(headRow);

      const codeRow = document.createElement('tr');
      codeRow.className = 'fe-code-row';
      const commentCell = document.createElement('td');
      commentCell.className = 'fe-comment-cell';
      const commentArea = document.createElement('textarea');
      commentArea.className = 'fe-comment';
      commentArea.rows = 7;
      commentCell.appendChild(commentArea);
      const buttonCell = document.createElement('td');
      buttonCell.className = 'fe-buttons';
      createButtons(run, commentArea, buttonCell);
      const modeCell = document.createElement('div');
      modeCell.className = 'fe-mode-slot';
      buttonCell.appendChild(modeCell);
      run.modeCell = modeCell;
      const sourceCell = document.createElement('td');
      sourceCell.className = 'fe-source-cell';
      codeRow.appendChild(commentCell);
      codeRow.appendChild(buttonCell);
      codeRow.appendChild(sourceCell);
      solutions.appendChild(codeRow);

      const commentsRow = document.createElement('tr');
      const commentsCell = document.createElement('td');
      commentsCell.className = 'fe-comments';
      commentsCell.colSpan = 3;
      commentsCell.textContent = 'Загрузка встроенных комментариев…';
      commentsRow.appendChild(commentsCell);
      solutions.appendChild(commentsRow);

      run.commentsCell = commentsCell;
      run.commentArea = commentArea;
      processSolution(sourceCell, run);
    }
    return solutions;
  }

  function addLoadButton(table) {
    if (document.querySelector('.fe-load-solutions')) return;
    const toolbar = document.createElement('div');
    toolbar.className = 'fe-toolbar';
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'fe-load-solutions';
    button.textContent = 'Загрузить и показать решения';
    button.addEventListener('click', function () {
      button.disabled = true;
      const solutions = makeSolutionTable(table);
      const wrapper = document.createElement('div');
      wrapper.className = 'fe-solutions-wrapper';
      wrapper.appendChild(solutions);
      toolbar.parentNode.insertBefore(wrapper, toolbar.nextSibling);
    });
    toolbar.appendChild(button);
    table.parentNode.insertBefore(toolbar, table.nextSibling);
  }

  function compareCells(first, second, ascending) {
    const left = first ? first.textContent.trim() : '';
    const right = second ? second.textContent.trim() : '';
    const leftNumber = Number(left);
    const rightNumber = Number(right);
    let result;
    if (left !== '' && right !== '' && Number.isFinite(leftNumber) && Number.isFinite(rightNumber)) {
      result = leftNumber - rightNumber;
    } else {
      result = left.localeCompare(right, 'ru');
    }
    return ascending ? result : -result;
  }

  function makeTableSortable(table) {
    if (!table.rows || !table.rows[0]) return;
    Array.from(table.rows[0].cells).forEach(function (header, index) {
      header.classList.add('fe-sortable');
      header.addEventListener('click', function () {
        const ascending = header.dataset.sortAsc !== '1';
        header.dataset.sortAsc = ascending ? '1' : '0';
        const rows = Array.from(table.rows).slice(1);
        rows.sort(function (first, second) {
          return compareCells(first.cells[index], second.cells[index], ascending);
        });
        for (const row of rows) table.appendChild(row);
      });
    });
  }

  function init() {
    if (!document.querySelector('input[name="filter_expr"]')) return;
    const table = findRunsTable();
    if (!table) return;
    makeTableSortable(table);
    addLoadButton(table);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
