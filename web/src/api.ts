export type Dialect = 'sqlite' | 'postgres'
export type OutputFormat = 'sql' | 'csv' | 'postgres_copy'
export type Validation = 'none' | 'sqlite' | 'postgres'

export type ColumnSettings = {
  min?: number
  max?: number
  values?: Array<string | number | boolean>
  null_probability?: number
}

export type TableSettings = {
  rows: number
  columns: Record<string, ColumnSettings>
}

export type SchemaInput = {
  sql: string
  dialect: Dialect
  seed: number
  default_rows: number
}

export type InspectResult = {
  tables: Array<{ name: string; rows: number }>
  dialect: Dialect
}

export type RunInput = SchemaInput & {
  realistic: boolean
  tables: Record<string, TableSettings>
  output_format: OutputFormat
  validate_with: Validation
}

export type FilePreview =
  | { name: string; kind: 'code'; lines: string[] }
  | { name: string; kind: 'table'; rows: string[][] }

export type RunResult = {
  id: string
  status: 'completed'
  format: OutputFormat
  tables: Array<{ name: string; rows: number }>
  previews: FilePreview[]
  metrics: {
    generation?: {
      total_rows?: number
      time_seconds?: number
      throughput_rows_per_second?: number
      output_file_size_bytes?: number
    }
    validation?: { sqlite?: string; postgres?: string }
  }
  log: string
  download_name: string
}

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  let response: Response
  try {
    response = await fetch(path, {
      ...options,
      headers: { 'Content-Type': 'application/json', ...options?.headers },
    })
  } catch {
    throw new Error('The SchemaForge server is unreachable. Start the API server and try again.')
  }
  const body = await response.json().catch(() => ({}))
  if (!response.ok) {
    const detail = body.detail
    if (typeof detail === 'object' && detail?.message) {
      throw new Error(detail.log ? `${detail.message}\n${detail.log}` : detail.message)
    }
    if (Array.isArray(detail) && detail.length) {
      throw new Error(detail.map((issue) => `${issue.loc?.slice(1).join('.') || 'Input'}: ${issue.msg}`).join('\n'))
    }
    throw new Error(typeof detail === 'string' ? detail : 'The request could not be completed.')
  }
  return body as T
}

export const api = {
  health: () => request<{ status: string; message?: string }>('/api/health'),
  example: () => request<{ name: string; sql: string }>('/api/examples/ecommerce'),
  inspect: (input: SchemaInput) =>
    request<InspectResult>('/api/schemas/inspect', { method: 'POST', body: JSON.stringify(input) }),
  generate: (input: RunInput) =>
    request<RunResult>('/api/runs', { method: 'POST', body: JSON.stringify(input) }),
  downloadUrl: (runId: string) => `/api/runs/${runId}/download`,
}
