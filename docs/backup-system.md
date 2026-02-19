# Backup System

Backups are stored as `.tar` archives in `{DATA_DIR}/backups/`. Job state is persisted to `jobs.json`.

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/index/{name}/backup` | Create async backup |
| GET | `/api/v1/backups` | List all backup files |
| GET | `/api/v1/backups/jobs` | List all backup jobs with status |
| POST | `/api/v1/backups/{name}/restore` | Restore backup to new index |
| DELETE | `/api/v1/backups/{name}` | Delete a backup file |
| GET | `/api/v1/backups/{name}/download` | Download backup (streaming) |
| POST | `/api/v1/backups/upload` | Upload a backup file |

---

## Concurrency Model

```
backup_in_progress (atomic<bool>, per-index)   flock on jobs.json (file lock)
├── Lock-free check for write operations        ├── Protects: jobs.json file
├── Scope: single CacheEntry                    ├── Scope: all job updates
├── Cost: ~1 ns (single CPU instruction)        └── LOCK_EX for writes, LOCK_SH for reads
└── Set via compare_exchange_strong                  (thread-safe file access)

operation_mutex (mutex, per-index)
├── Protects: index data during save + tar
├── Scope: single index
└── Held for: seconds/minutes
```

**Hybrid approach:** Atomic flag for write rejection (fast) + flock for job persistence (no in-memory map). Backup thread holds `operation_mutex` for minutes (save + tar). The atomic flag gives instant lock-free 409 rejection while job updates go directly to disk with flock.

**Write path is lock-free for backup checks:**

```
Write:   atomic load backup_in_progress → lock operation_mutex → release
Backup:  compare_exchange_strong(flag) → flock jobs.json + write → lock operation_mutex → release → clear flag → flock jobs.json + write
```

---

## Flows

### Create Backup (Async)

```
POST /index/X/backup → validateBackupName() → check no duplicate on disk
→ generate job_id → compare_exchange_strong(backup_in_progress, false→true)
→ [flock LOCK_EX] register job to jobs.json [unlock]
→ spawn detached thread → return 202 { job_id }
```

`compare_exchange_strong` atomically rejects if another backup is already running for this index.

**Background thread** (`executeBackupJob`):

```
[flock LOCK_SH] verify job exists in jobs.json [unlock]
→ check disk space (need 2x index size) → read metadata
→ [LOCK operation_mutex] saveIndexInternal → write metadata.json → create .tmp_{name}.tar → cleanup metadata.json [UNLOCK operation_mutex]
→ clear backup_in_progress flag (atomic store false)
→ rename .tmp_ → final tar (atomic)
→ [flock LOCK_EX] mark COMPLETED in jobs.json [unlock]
```

**On failure**: cleanup temp files → clear backup_in_progress flag → [flock LOCK_EX] mark job FAILED in jobs.json [unlock].

### Write During Backup

```
addVectors/deleteVectors/updateFilters/deleteByFilter/deleteIndex
→ checkBackupInProgress(): atomic load backup_in_progress (NO lock needed)
→ if backup active: immediately throw 409 "Cannot modify index while backup is in progress"
→ if no backup: [LOCK operation_mutex] do the write [UNLOCK] → 200 OK
  (blocks normally if another write holds operation_mutex — same as before)
```

### Restore Backup

```
POST /backups/{name}/restore
→ validate name → check tar exists → check target index does NOT exist
→ extract tar → read metadata.json → copy files to target dir
→ register in MetadataManager → cleanup temp dir → loadIndex()
→ 201 OK
```

### Download (Streaming)

```
GET /backups/{name}/download
→ check file exists → set_static_file_info_unsafe() (Crow streams from disk in chunks)
→ Server RAM stays constant (~8 MB) even for 23 GB+ files
```

### Upload

```
POST /backups/upload (multipart)
→ parse multipart → validate .tar extension + name → check no duplicate → write to disk
→ 201 OK

NOTE: Upload currently buffers entire file in RAM (Crow multipart parser limitation).
```

---

## Safety Checks

| # | Check | Where |
|---|-------|-------|
| 1 | **One backup per index** — `compare_exchange_strong` on atomic flag prevents duplicate backups per index | createBackupAsync |
| 2 | **Write protection** — all write ops do lock-free atomic check, get instant 409 if backup active | addVectors, deleteVectors, updateFilters, deleteByFilter, deleteIndex |
| 3 | **Name validation** — alphanumeric, underscores, hyphens only; max 200 chars | validateBackupName |
| 4 | **Duplicate prevention** — checked at creation AND inside background thread | createBackupAsync, executeBackupJob, upload |
| 5 | **Disk space** — requires 2x index size available | executeBackupJob |
| 6 | **Atomic tar** — writes to .tmp_ first, then renames | executeBackupJob |
| 7 | **Crash recovery** — on startup: mark stale IN_PROGRESS as FAILED, delete .tmp_ files. Atomic flag resets to `false` on fresh `CacheEntry` creation | loadJobs, cleanupIncompleteBackups |
| 8 | **Restore safety** — target must not exist, metadata must be valid, cleanup on failure | restoreBackup |
| 9 | **Job persistence** — flock (LOCK_EX) on jobs.json for thread-safe read-modify-write on every status change | modifyJobsFile |
