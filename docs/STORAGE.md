# Storage layout

Suggested removable-card layout (created only after the user confirms formatting or initialization):

```text
/CMAIR-OS/
  apps/        installed app packages
  data/        app data, scoped by app ID
  media/       music, photos and videos
  projects/    ACard Studio and code projects
  saves/       game saves
  downloads/   user downloads
  backups/     user-created backups
  logs/        bounded diagnostic logs
```

The internal flash is reserved for boot, core firmware, recovery metadata and small essential settings. The OS must still boot into a useful recovery state when the microSD is missing.

Updates must use temporary files and atomic rename where supported. Never erase the only working firmware image before a replacement has been verified.
