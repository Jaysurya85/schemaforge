import { useEffect, useMemo, useState } from 'react'
import {
  ArrowDownToLine, ArrowRight, Braces, Check, ChevronDown, CircleAlert,
  CircleCheck, Code2, Database, FileCode2, Github, LoaderCircle, Play,
  Moon, Plus, RotateCcw, Settings2, Sparkles, Sun, Table2, Trash2, WandSparkles,
} from 'lucide-react'
import { api, type AIReview, type ColumnSettings, type ConfigDraft, type Dialect, type FilePreview, type OutputFormat, type RunInput, type RunResult, type TableSettings, type Validation } from './api'
import './styles.css'

const starterSql = `CREATE TABLE customers (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  email TEXT UNIQUE NOT NULL
);

CREATE TABLE orders (
  id INTEGER PRIMARY KEY,
  customer_id INTEGER NOT NULL REFERENCES customers(id),
  total DECIMAL(10, 2) NOT NULL
);`

type RuleDraft = { table: string; column: string; type: 'values' | 'range' | 'nulls'; value: string }
type Rule = RuleDraft & { id: number }

function formatNumber(value: number | undefined, digits = 0) {
  return typeof value === 'number' && Number.isFinite(value)
    ? new Intl.NumberFormat('en-US', { maximumFractionDigits: digits }).format(value)
    : '—'
}

const integerTypes = new Set(['INT', 'INTEGER', 'SMALLINT', 'BIGINT', 'LONG'])
const decimalTypes = new Set(['DECIMAL', 'FLOAT', 'DOUBLE', 'REAL'])

function parseRule(rule: Rule, columnType: string): ColumnSettings {
  const label = `${rule.table}.${rule.column}`
  if (rule.type === 'range') {
    if (!integerTypes.has(columnType) && !decimalTypes.has(columnType)) {
      throw new Error(`A number range requires a numeric column. ${label} is ${columnType}.`)
    }
    const parts = rule.value.split(',').map((part) => part.trim())
    const [min, max] = parts.map(Number)
    if (parts.length !== 2 || parts.some((part) => !part) || !Number.isFinite(min) || !Number.isFinite(max) || min > max ||
      (integerTypes.has(columnType) && (!Number.isSafeInteger(min) || !Number.isSafeInteger(max)))) {
      throw new Error(`Enter a valid min, max range for ${label}.`)
    }
    return { min, max }
  }
  if (rule.type === 'nulls') {
    const probability = Number(rule.value)
    if (!Number.isFinite(probability) || probability < 0 || probability > 100) {
      throw new Error(`Enter a percentage from 0 to 100 for ${rule.table}.${rule.column}.`)
    }
    return { null_probability: probability / 100 }
  }
  let values: string[]
  if (rule.value.trim().startsWith('[')) {
    try {
      const parsed = JSON.parse(rule.value)
      if (!Array.isArray(parsed) || parsed.some((value) => !['string', 'number', 'boolean'].includes(typeof value))) {
        throw new Error('Invalid list')
      }
      values = parsed.map(String)
    } catch {
      throw new Error(`Enter a valid value list for ${label}.`)
    }
  } else {
    values = rule.value.split(',').map((value) => value.trim()).filter(Boolean)
  }
  if (!values.length) throw new Error(`Add at least one value for ${label}.`)
  if (integerTypes.has(columnType)) {
    const numbers = values.map(Number)
    if (values.some((value) => !/^[+-]?\d+$/.test(value)) || numbers.some((value) => !Number.isSafeInteger(value))) {
      throw new Error(`Enter whole numbers for ${label}.`)
    }
    return { values: numbers }
  }
  if (decimalTypes.has(columnType)) {
    const numbers = values.map(Number)
    if (numbers.some((value) => !Number.isFinite(value))) {
      throw new Error(`Enter numbers for ${label}.`)
    }
    return { values: numbers }
  }
  if (columnType === 'BOOLEAN') {
    if (values.some((value) => !['true', 'false'].includes(value.toLowerCase()))) {
      throw new Error(`Enter true or false for ${label}.`)
    }
    return { values: values.map((value) => value.toLowerCase() === 'true') }
  }
  return { values }
}

function runSignature(input: RunInput): string {
  const tables = Object.fromEntries(Object.entries(input.tables).sort(([a], [b]) => a.localeCompare(b)).map(([name, table]) => [
    name, { rows: table.rows, columns: Object.fromEntries(Object.entries(table.columns ?? {}).sort(([a], [b]) => a.localeCompare(b)).map(([column, settings]) => [
      column, Object.fromEntries(Object.entries(settings).filter(([, value]) => value !== null && value !== undefined).sort(([a], [b]) => a.localeCompare(b))),
    ])) },
  ]))
  return JSON.stringify({ sql: input.sql, dialect: input.dialect, seed: input.seed,
    default_rows: input.default_rows, realistic: input.realistic, output_format: input.output_format,
    validate_with: input.validate_with, tables })
}

function rulesFromRequest(input: RunInput): Rule[] {
  const result: Rule[] = []
  let id = 1
  for (const [table, settings] of Object.entries(input.tables)) {
    for (const [column, columnSettings] of Object.entries(settings.columns ?? {})) {
      if (columnSettings.values?.length) result.push({ id: id++, table, column, type: 'values', value: JSON.stringify(columnSettings.values) })
      else if (columnSettings.min !== undefined && columnSettings.min !== null && columnSettings.max !== undefined && columnSettings.max !== null) {
        result.push({ id: id++, table, column, type: 'range', value: `${columnSettings.min}, ${columnSettings.max}` })
      }
      if (columnSettings.null_probability !== undefined && columnSettings.null_probability !== null) {
        result.push({ id: id++, table, column, type: 'nulls', value: String(columnSettings.null_probability * 100) })
      }
    }
  }
  return result
}

function Preview({ preview }: { preview: FilePreview }) {
  if (preview.kind === 'table') {
    const [header, ...rows] = preview.rows
    return <div className="table-scroll"><table className="preview-table"><thead><tr>{header?.map((cell, index) => <th key={index}>{cell}</th>)}</tr></thead><tbody>{rows.map((row, index) => <tr key={index}>{row.map((cell, cellIndex) => <td key={cellIndex}>{cell || <span className="muted">NULL</span>}</td>)}</tr>)}</tbody></table></div>
  }
  return <div className="code-preview"><div className="line-numbers" aria-hidden="true">{preview.lines.map((_, index) => <span key={index}>{index + 1}</span>)}</div><pre>{preview.lines.join('\n')}</pre></div>
}

function App() {
  const [sql, setSql] = useState(starterSql)
  const [dialect, setDialect] = useState<Dialect>('sqlite')
  const [format, setFormat] = useState<OutputFormat>('sql')
  const [validation, setValidation] = useState<Validation>('sqlite')
  const [seed, setSeed] = useState(42)
  const [defaultRows, setDefaultRows] = useState(100)
  const [realistic, setRealistic] = useState(true)
  const [tables, setTables] = useState<Record<string, TableSettings>>({})
  const [schemaColumns, setSchemaColumns] = useState<Record<string, string[]>>({})
  const [schemaColumnTypes, setSchemaColumnTypes] = useState<Record<string, Record<string, string>>>({})
  const [inspected, setInspected] = useState(false)
  const [rules, setRules] = useState<Rule[]>([])
  const [ruleDraft, setRuleDraft] = useState<RuleDraft>({ table: '', column: '', type: 'values', value: '' })
  const [advancedOpen, setAdvancedOpen] = useState(false)
  const [engineStatus, setEngineStatus] = useState<'checking' | 'ready' | 'unavailable'>('checking')
  const [theme, setTheme] = useState<'light' | 'dark'>(() => window.localStorage.getItem('schemaforge-theme') === 'dark' ? 'dark' : 'light')
  const [busy, setBusy] = useState<'inspect' | 'plan' | 'generate' | null>(null)
  const [error, setError] = useState('')
  const [testingGoal, setTestingGoal] = useState('')
  const [review, setReview] = useState<(AIReview & { origin: string }) | null>(null)
  const [acceptedDraft, setAcceptedDraft] = useState<ConfigDraft | null>(null)
  const [run, setRun] = useState<RunResult | null>(null)
  const [activePreview, setActivePreview] = useState(0)

  useEffect(() => {
    api.health().then((result) => setEngineStatus(result.status === 'ready' ? 'ready' : 'unavailable')).catch(() => setEngineStatus('unavailable'))
  }, [])

  useEffect(() => {
    document.documentElement.dataset.theme = theme
    window.localStorage.setItem('schemaforge-theme', theme)
  }, [theme])

  useEffect(() => {
    setRun(null)
  }, [sql, dialect, format, validation, seed, defaultRows, realistic, tables, rules])

  const totalRows = useMemo(() => Object.values(tables).reduce((sum, table) => sum + table.rows, 0), [tables])
  const canGenerate = inspected && Object.keys(tables).length > 0 && !busy && engineStatus === 'ready'

  function buildRunInput(): RunInput {
    const configuredTables = structuredClone(tables)
    for (const rule of rules) {
      configuredTables[rule.table].columns[rule.column] = {
        ...configuredTables[rule.table].columns[rule.column],
        ...parseRule(rule, schemaColumnTypes[rule.table]?.[rule.column] ?? ''),
      }
    }
    return { sql, dialect, seed, default_rows: defaultRows, realistic,
      tables: configuredTables, output_format: format, validate_with: validation }
  }

  let currentSignature = ''
  try { currentSignature = runSignature(buildRunInput()) } catch { /* The form shows the rule error on submit. */ }
  const acceptedCurrent = !!acceptedDraft && currentSignature === runSignature(acceptedDraft.request)
  const reviewVisible = review && (review.origin === `${currentSignature}:${testingGoal.trim()}` || acceptedCurrent)

  function updateSql(value: string) {
    setSql(value)
    setInspected(false)
    setTables({})
    setSchemaColumns({})
    setSchemaColumnTypes({})
    setRules([])
    setRun(null)
    setError('')
  }

  async function loadExample() {
    setError('')
    try {
      const example = await api.example()
      updateSql(example.sql)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'Could not load the example.')
    }
  }

  async function inspect() {
    setBusy('inspect')
    setError('')
    setRun(null)
    try {
      const result = await api.inspect({ sql, dialect, seed, default_rows: defaultRows })
      setTables(Object.fromEntries(result.tables.map((table) => [table.name, { rows: table.rows, columns: {} }])))
      setSchemaColumns(Object.fromEntries(result.tables.map((table) => [table.name, table.columns])))
      setSchemaColumnTypes(Object.fromEntries(result.tables.map((table) => [table.name, table.column_types])))
      setRules([])
      setInspected(true)
      setRuleDraft((draft) => ({ ...draft, table: result.tables[0]?.name ?? '', column: result.tables[0]?.columns[0] ?? '' }))
    } catch (cause) {
      setInspected(false)
      setError(cause instanceof Error ? cause.message : 'Could not inspect the schema.')
    } finally {
      setBusy(null)
    }
  }

  function changeDialect(value: Dialect) {
    setDialect(value)
    if (value === 'sqlite') {
      setFormat('sql')
      setValidation('sqlite')
    } else {
      setValidation('none')
    }
    setInspected(false)
    setRun(null)
  }

  function changeFormat(value: OutputFormat) {
    setFormat(value)
    if (value === 'postgres_copy') {
      setDialect('postgres')
      setValidation('none')
      setInspected(false)
    } else if (value === 'csv') {
      setValidation('none')
    } else if (dialect === 'sqlite') {
      setValidation('sqlite')
    }
    setRun(null)
  }

  function addRule() {
    const column = ruleDraft.column.trim()
    if (!ruleDraft.table || !schemaColumns[ruleDraft.table]?.includes(column)) {
      setError('Choose a column from the inspected schema before adding a rule.')
      return
    }
    const candidate = { ...ruleDraft, column, id: Date.now() }
    try {
      parseRule(candidate, schemaColumnTypes[candidate.table]?.[column] ?? '')
      if (rules.some((rule) => rule.table === candidate.table && rule.column === column && rule.type === candidate.type)) {
        throw new Error('A rule of this type already exists for that column.')
      }
      setRules((current) => [...current, candidate])
      setRuleDraft((draft) => ({ ...draft, value: '' }))
      setError('')
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'Could not add the rule.')
    }
  }

  async function generate() {
    setBusy('generate')
    setError('')
    setRun(null)
    try {
      const result = acceptedCurrent && acceptedDraft
        ? await api.generateDraft(acceptedDraft.id)
        : await api.generate(buildRunInput())
      setRun(result)
      setAcceptedDraft(null)
      setActivePreview(0)
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'Generation failed.')
    } finally {
      setBusy(null)
    }
  }

  async function planWithAI() {
    setBusy('plan')
    setError('')
    setReview(null)
    setAcceptedDraft(null)
    try {
      const input = buildRunInput()
      const result = await api.plan(input, testingGoal.trim())
      setReview({ ...result, origin: `${runSignature(input)}:${testingGoal.trim()}` })
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : 'AI planning failed.')
    } finally {
      setBusy(null)
    }
  }

  function applyReview() {
    if (!review) return
    const input = review.draft.request
    setTables(Object.fromEntries(Object.entries(input.tables).map(([name, settings]) => [name, { rows: settings.rows, columns: {} }])))
    setRules(rulesFromRequest(input))
    setAcceptedDraft(review.draft)
    setAdvancedOpen(true)
    setError('')
  }

  return <div className="app-shell">
    <header className="site-header">
      <div className="header-inner">
        <a className="brand" href="#top" aria-label="SchemaForge home"><span className="brand-mark" aria-hidden="true"><span /><span /><span /><span /></span><span>schemaforge</span></a>
        <nav className="header-nav" aria-label="Main navigation"><a className="nav-active" href="#workspace">Workspace</a><a href="#how-it-works">How it works</a><a href="https://github.com/Jaysurya85/schemaforge" target="_blank" rel="noreferrer"><Github size={15} /> GitHub</a></nav>
        <div className={`engine-pill ${engineStatus}`}><span className="status-dot" />{engineStatus === 'ready' ? 'Engine ready' : engineStatus === 'checking' ? 'Checking engine' : 'Engine offline'}</div>
        <button className="theme-button" type="button" aria-label={`Switch to ${theme === 'light' ? 'dark' : 'light'} mode`} title={`Switch to ${theme === 'light' ? 'dark' : 'light'} mode`} onClick={() => setTheme(theme === 'light' ? 'dark' : 'light')}>{theme === 'light' ? <Moon size={16} /> : <Sun size={16} />}</button>
      </div>
    </header>

    <main id="top">
      <section className="hero container">
        <div className="eyebrow"><span className="eyebrow-rule" /> THE SCHEMA WORKSPACE <span className="version">/ 01</span></div>
        <h1>Realistic data, from<br /><em>your schema.</em></h1>
        <p>Turn SQL tables into connected, usable test data. Shape the rows, choose your output, and see what SchemaForge creates.</p>
        <div className="hero-meta"><span><Braces size={15} /> Start with SQL</span><span><ArrowRight size={15} /> Export in three formats</span><span><CircleCheck size={15} /> Validate the result</span></div>
      </section>

      <section className="workspace container" id="workspace" aria-label="SchemaForge workspace">
        <div className="workspace-heading"><div><span className="section-kicker">WORKSPACE</span><h2>Generate a dataset</h2></div><span className="workspace-note">A local workspace powered by the C++ engine <span className="note-line" /></span></div>
        <div className="workspace-grid">
          <div className="input-column">
            <div className="panel schema-panel">
              <div className="panel-heading"><div className="step-number">01</div><div><h3>Define your schema</h3><p>Paste your CREATE TABLE statements to begin.</p></div><button className="text-action" onClick={loadExample} type="button"><RotateCcw size={14} /> Use example</button></div>
              <div className="field-row"><label htmlFor="dialect">SQL dialect</label><div className="segmented" role="group" aria-label="SQL dialect"><button className={dialect === 'sqlite' ? 'selected' : ''} onClick={() => changeDialect('sqlite')} type="button">SQLite</button><button className={dialect === 'postgres' ? 'selected' : ''} onClick={() => changeDialect('postgres')} type="button">PostgreSQL</button></div></div>
              <div className="editor-wrap"><div className="editor-bar"><span className="editor-file"><FileCode2 size={14} /> schema.sql</span><span>SQL</span></div><textarea aria-label="SQL schema" spellCheck={false} value={sql} onChange={(event) => updateSql(event.target.value)} placeholder="CREATE TABLE ..." /></div>
              <div className="schema-footer"><span>Only CREATE TABLE statements are accepted</span><button className="button button-secondary" onClick={inspect} disabled={!!busy || engineStatus !== 'ready'} type="button">{busy === 'inspect' ? <LoaderCircle size={16} className="spin" /> : <Code2 size={16} />} Inspect schema</button></div>
            </div>

            <div className={`panel config-panel ${!inspected ? 'awaiting' : ''}`}>
              <div className="panel-heading"><div className="step-number">02</div><div><h3>Shape the data</h3><p>{inspected ? `${Object.keys(tables).length} tables found. Set how much data to create.` : 'Inspect your schema to configure tables.'}</p></div></div>
              <div className="field-grid"><div className="field"><label htmlFor="default-rows">Rows per table</label><input id="default-rows" type="number" min="0" max="2000000" value={defaultRows} onChange={(event) => setDefaultRows(Number(event.target.value))} disabled={inspected} /><small>{inspected ? 'Edit each table below' : 'Applied when inspecting'}</small></div><div className="field"><label htmlFor="seed">Random seed</label><input id="seed" type="number" min="0" max="4294967295" value={seed} onChange={(event) => setSeed(Number(event.target.value))} /><small>Repeatable results</small></div></div>
              {inspected && <div className="table-config"><div className="field-label">TABLES <span>{formatNumber(totalRows)} total rows</span></div>{Object.entries(tables).map(([name, settings]) => <div className="table-row" key={name}><span className="table-name"><Table2 size={16} />{name}</span><div className="row-input"><input aria-label={`Rows for ${name}`} type="number" min="0" max="2000000" value={settings.rows} onChange={(event) => setTables((current) => ({ ...current, [name]: { ...current[name], rows: Number(event.target.value) } }))} /><span>rows</span></div></div>)}</div>}
              <div className="toggle-row"><div><strong>Realistic values</strong><span>Generate more natural names, dates, and text</span></div><button className={`toggle ${realistic ? 'on' : ''}`} type="button" role="switch" aria-checked={realistic} aria-label="Realistic values" onClick={() => setRealistic(!realistic)}><span /></button></div>
              {inspected && <div className="advanced"><button className="advanced-trigger" type="button" aria-expanded={advancedOpen} onClick={() => setAdvancedOpen(!advancedOpen)}><span><Settings2 size={16} /> Column rules <span className="rule-count">{rules.length}</span></span><ChevronDown size={16} className={advancedOpen ? 'chevron-open' : ''} /></button>{advancedOpen && <div className="advanced-body"><p>Override individual columns with a value list, numeric range, or null rate.</p>{rules.map((rule) => <div className="saved-rule" key={rule.id}><span><strong>{rule.table}.{rule.column}</strong> · {rule.type === 'nulls' ? `${rule.value}% nulls` : rule.type === 'range' ? `range ${rule.value}` : rule.value}</span><button type="button" aria-label={`Remove rule for ${rule.table}.${rule.column}`} onClick={() => setRules((current) => current.filter((item) => item.id !== rule.id))}><Trash2 size={14} /></button></div>)}<div className="rule-fields"><select aria-label="Rule table" value={ruleDraft.table} onChange={(event) => setRuleDraft({ ...ruleDraft, table: event.target.value, column: schemaColumns[event.target.value]?.[0] ?? '' })}>{Object.keys(tables).map((name) => <option key={name}>{name}</option>)}</select><select aria-label="Rule column" value={ruleDraft.column} onChange={(event) => setRuleDraft({ ...ruleDraft, column: event.target.value })}>{(schemaColumns[ruleDraft.table] ?? []).map((name) => <option key={name} value={name}>{name}</option>)}</select><select aria-label="Rule type" value={ruleDraft.type} onChange={(event) => setRuleDraft({ ...ruleDraft, type: event.target.value as RuleDraft['type'], value: '' })}><option value="values">Value list</option><option value="range">Number range</option><option value="nulls">Null rate</option></select><input aria-label="Rule value" placeholder={ruleDraft.type === 'range' ? 'min, max' : ruleDraft.type === 'nulls' ? 'Percent (0–100)' : 'value 1, value 2'} value={ruleDraft.value} onChange={(event) => setRuleDraft({ ...ruleDraft, value: event.target.value })} /></div><button className="small-button" type="button" onClick={addRule}><Plus size={14} /> Add rule</button></div>}</div>}
            </div>

            {inspected && <div className="panel ai-panel"><div className="panel-heading"><div className="step-number ai-step"><Sparkles size={16} /></div><div><h3>Plan a test with AI</h3><p>Describe the data shape you need. Review every change before generating.</p></div></div><label className="ai-label" htmlFor="testing-goal">What would you like to test?</label><textarea id="testing-goal" className="ai-prompt" value={testingGoal} onChange={(event) => setTestingGoal(event.target.value)} placeholder="Example: Create 1,000 customers and 3,000 orders. Give customers a mix of gold and silver tiers." rows={3} /><div className="ai-actions"><span>Planning sends your schema, current settings, and this request to the configured AI provider.</span><button className="button button-secondary" type="button" disabled={!!busy || testingGoal.trim().length < 3 || engineStatus !== 'ready'} onClick={planWithAI}>{busy === 'plan' ? <LoaderCircle size={16} className="spin" /> : <Sparkles size={16} />}{busy === 'plan' ? 'Planning…' : 'Create plan'}</button></div>{reviewVisible && <div className="ai-review"><div className="ai-review-heading"><strong>Review the proposed changes</strong><span>Validated by SchemaForge</span></div><div className="ai-plan-list">{review.plan.table_rows.map((item) => <div key={`rows-${item.table}`}><Table2 size={14} /><span>{item.table}: {formatNumber(item.rows)} rows</span></div>)}{review.plan.number_ranges.map((item) => <div key={`range-${item.table}-${item.column}`}><Settings2 size={14} /><span>{item.table}.{item.column}: {item.min} to {item.max}</span></div>)}{review.plan.value_lists.map((item) => <div key={`values-${item.table}-${item.column}`}><Settings2 size={14} /><span>{item.table}.{item.column}: {item.values.join(', ')}</span></div>)}{review.plan.null_rates.map((item) => <div key={`nulls-${item.table}-${item.column}`}><Settings2 size={14} /><span>{item.table}.{item.column}: {formatNumber(item.probability * 100, 1)}% null</span></div>)}{!review.plan.table_rows.length && !review.plan.number_ranges.length && !review.plan.value_lists.length && !review.plan.null_rates.length && <p>No settings changed.</p>}</div>{review.plan.assumptions.length > 0 && <div className="ai-notes"><strong>Assumptions</strong><ul>{review.plan.assumptions.map((item, index) => <li key={index}>{item}</li>)}</ul></div>}{review.plan.unapplied.length > 0 && <div className="ai-notes unapplied"><strong>Not applied</strong><ul>{review.plan.unapplied.map((item, index) => <li key={index}>{item.request}: {item.explanation}</li>)}</ul></div>}<details className="yaml-preview" open><summary>Generated configuration · YAML</summary><pre>{review.draft.yaml}</pre></details>{acceptedCurrent ? <div className="ai-applied"><CircleCheck size={16} /> Plan applied to the controls. Generate to use this exact configuration.</div> : <button className="button button-secondary ai-apply" type="button" onClick={applyReview}><Check size={16} /> Apply plan to controls</button>}</div>}{acceptedDraft && !acceptedCurrent && <p className="ai-stale">The controls changed after applying the plan. Create a new plan to review updated YAML.</p>}</div>}

            <div className="panel output-panel"><div className="panel-heading"><div className="step-number">03</div><div><h3>Choose your output</h3><p>Pick a format and validation method.</p></div></div><div className="format-options" role="group" aria-label="Output format"><button className={format === 'sql' ? 'active' : ''} onClick={() => changeFormat('sql')} type="button"><FileCode2 size={17} /><strong>SQL</strong><span>Insert statements</span></button><button className={format === 'csv' ? 'active' : ''} onClick={() => changeFormat('csv')} type="button"><Table2 size={17} /><strong>CSV</strong><span>One file per table</span></button><button className={format === 'postgres_copy' ? 'active' : ''} onClick={() => changeFormat('postgres_copy')} type="button"><Database size={17} /><strong>PG COPY</strong><span>Bulk import script</span></button></div><div className="validation-row"><label htmlFor="validation">Validate with</label><select id="validation" value={validation} onChange={(event) => setValidation(event.target.value as Validation)}><option value="none">No validation</option>{format === 'sql' && dialect === 'sqlite' && <option value="sqlite">SQLite</option>}{dialect === 'postgres' && format !== 'csv' && <option value="postgres">PostgreSQL</option>}</select></div></div>

            {error && <div className="error-box" role="alert"><CircleAlert size={18} /><pre>{error}</pre></div>}
            {engineStatus === 'unavailable' && <div className="error-box engine-error"><CircleAlert size={18} /><div><strong>Engine unavailable</strong><p>Build the C++ CLI and start the API server to generate data.</p></div></div>}
            <button className="button button-primary generate-button" disabled={!canGenerate} onClick={generate} type="button">{busy === 'generate' ? <LoaderCircle size={18} className="spin" /> : <Play size={17} fill="currentColor" />} {busy === 'generate' ? 'Generating dataset…' : 'Generate dataset'} <ArrowRight size={17} /></button>
            <p className="under-button">Dataset generation runs locally. AI planning sends your schema and request to the configured provider.</p>
          </div>

          <div className="result-column"><div className="panel result-panel"><div className="result-top"><div><span className="section-kicker">OUTPUT / PREVIEW</span><h3>{run ? 'Your dataset is ready' : 'The result will appear here'}</h3></div>{run && <span className="success-pill"><Check size={13} /> Complete</span>}</div>{run ? <><div className="metrics"><div><span>ROWS GENERATED</span><strong>{formatNumber(run.metrics.generation?.total_rows ?? run.tables.reduce((sum, table) => sum + table.rows, 0))}</strong></div><div><span>GENERATION TIME</span><strong>{typeof run.metrics.generation?.time_seconds === 'number' ? `${formatNumber(run.metrics.generation.time_seconds, 2)}s` : '—'}</strong></div><div><span>OUTPUT SIZE</span><strong>{typeof run.metrics.generation?.output_file_size_bytes === 'number' ? `${formatNumber(run.metrics.generation.output_file_size_bytes / 1024, 1)} KB` : '—'}</strong></div></div><div className="result-section-label"><span>FILE PREVIEW</span><span>First rows / lines</span></div><div className="preview-tabs" role="tablist" aria-label="Output files">{run.previews.map((preview, index) => <button role="tab" aria-selected={activePreview === index} className={activePreview === index ? 'active' : ''} key={preview.name} onClick={() => setActivePreview(index)} type="button">{preview.kind === 'table' ? <Table2 size={14} /> : <FileCode2 size={14} />}{preview.name}</button>)}</div>{run.previews[activePreview] && <Preview preview={run.previews[activePreview]} />}<div className="result-footer"><div><CircleCheck size={17} /><span>{validation === 'none' ? 'Generation complete' : `${validation === 'sqlite' ? 'SQLite' : 'PostgreSQL'} validation: ${run.metrics.validation?.[validation] ?? 'complete'}`}</span></div><a className="button button-primary download-button" href={api.downloadUrl(run.id)} download={run.download_name}><ArrowDownToLine size={16} /> Download {format === 'csv' ? 'ZIP' : 'SQL'}</a></div><details className="engine-log"><summary>Engine log</summary><pre>{run.log}</pre></details></> : <div className="empty-result"><div className="empty-visual"><div className="empty-icon"><WandSparkles size={30} strokeWidth={1.4} /></div><div className="empty-track"><span /><span /><span /></div><div className="empty-output"><Table2 size={22} strokeWidth={1.5} /><span>DATA</span></div></div><h4>From schema to usable data</h4><p>Inspect your tables, tune the row counts, then generate. A preview and download will appear right here.</p><div className="empty-steps"><span><i>1</i> Schema</span><ArrowRight size={14} /><span><i>2</i> Configure</span><ArrowRight size={14} /><span><i>3</i> Export</span></div></div>}</div><div className="aside-note"><Sparkles size={16} /><span>Built for local iteration with the open source SchemaForge engine.</span></div></div>
        </div>
      </section>

      <section className="how-section container" id="how-it-works"><div className="how-heading"><span className="section-kicker">THE FLOW</span><h2>Three steps. Real relationships.</h2><p>SchemaForge reads the structure you already have and produces data that respects its constraints.</p></div><div className="how-grid"><div><span>01 / INPUT</span><Braces size={24} /><h3>Bring a schema</h3><p>Paste SQL CREATE TABLE statements, including primary keys and foreign keys.</p></div><div><span>02 / SHAPE</span><Settings2 size={24} /><h3>Set the details</h3><p>Choose rows, seed, realistic values, and rules for specific columns.</p></div><div><span>03 / EXPORT</span><ArrowDownToLine size={24} /><h3>Take your data</h3><p>Preview and download SQL, CSV, or PostgreSQL COPY output.</p></div></div></section>
    </main>
    <footer className="site-footer"><div className="container"><span className="brand footer-brand"><span className="brand-mark" aria-hidden="true"><span /><span /><span /><span /></span><span>schemaforge</span></span><span>Structured data, made useful.</span><a href="https://github.com/Jaysurya85/schemaforge" target="_blank" rel="noreferrer">View on GitHub <ArrowRight size={14} /></a></div></footer>
  </div>
}

export default App
