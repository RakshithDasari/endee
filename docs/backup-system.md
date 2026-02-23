# Backup System

Backups are stored as `.tar` archives in per-user directories: `{DATA_DIR}/backups/{username}/`. Active backup state is tracked in-memory.

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/v1/index/{name}/backup` | Create async backup |
| GET | `/api/v1/backups` | List all backup files |
| GET | `/api/v1/backups/active` | Check active backup for current user |
| GET | `/api/v1/backups/{name}/info` | Get backup metadata (read from .tar) |
| POST | `/api/v1/backups/{name}/restore` | Restore backup to new index |
| DELETE | `/api/v1/backups/{name}` | Delete a backup file |
| GET | `/api/v1/backups/{name}/download` | Download backup (streaming) |
| POST | `/api/v1/backups/upload` | Upload a backup file |

---

## Concurrency Model

```
operation_mutex (mutex, per-index)
├── Protects: index data during save + tar
├── Scope: single index
├── Held for: seconds/minutes (save + tar creation)
└── Write operations block until mutex is available
```

**Simple approach:** No atomic flags or file locks. The backup thread holds `operation_mutex` while saving and creating the tar. Write operations that arrive during backup simply block on the mutex until the backup releases it. One active backup per user is enforced via in-memory map.

**Write path during backup:**

```
Write:   lock operation_mutex → do the write → release
Backup:  lock operation_mutex → save + tar → release
```

If backup holds the mutex, writes block until it completes. Normal write-vs-write contention works the same way.

---

## Flows

### Create Backup (Async)

```
POST /index/X/backup → validateBackupName() → check no duplicate .tar on disk
→ check active_user_backups_[username] empty (one per user)
→ insert into active_user_backups_ map
→ set entry.active_backup_job_id = backup_name
→ spawn detached thread → return 202 { backup_name }
```

**Background thread** (`executeBackupJob`):

```
→ check disk space (need 2x index size) → read metadata
→ [LOCK operation_mutex] saveIndexInternal → write metadata.json → create .tmp_{name}.tar → cleanup metadata.json [UNLOCK operation_mutex]
→ rename .tmp_ → final tar (atomic)
→ erase from active_user_backups_, clear entry.active_backup_job_id
```

**On failure**: cleanup temp files → erase from active_user_backups_ → clear entry.active_backup_job_id.

### Write During Backup

```
addVectors/deleteVectors/updateFilters/deleteByFilter/deleteIndex
→ [LOCK operation_mutex] do the write [UNLOCK] → 200 OK
  (blocks if backup holds operation_mutex — resumes after backup completes)
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

### Get Backup Info

```
GET /backups/{name}/info
→ locate {DATA_DIR}/backups/{username}/{name}.tar
→ read metadata.json from inside .tar (via libarchive, no full extraction)
→ return metadata JSON (original_index, timestamp, size_mb, params)
```

---

## Safety Checks

| # | Check | Where |
|---|-------|-------|
| 1 | **One backup per user** — `active_user_backups_` map rejects if user already has active backup | createBackupAsync |
| 2 | **Write blocking** — writes block on `operation_mutex` until backup completes | addVectors, deleteVectors, updateFilters, deleteByFilter, deleteIndex |
| 3 | **Name validation** — alphanumeric, underscores, hyphens only; max 200 chars | validateBackupName |
| 4 | **Duplicate prevention** — checks if .tar file already exists on disk | createBackupAsync, upload |
| 5 | **Disk space** — requires 2x index size available | executeBackupJob |
| 6 | **Atomic tar** — writes to .tmp_ first, then renames | executeBackupJob |
| 7 | **Crash recovery** — on startup: delete `.tmp_*.tar` files in each user subfolder | recoverBackupState |
| 8 | **Restore safety** — target must not exist, metadata must be valid, cleanup on failure | restoreBackup |
