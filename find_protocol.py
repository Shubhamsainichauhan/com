import os

search_dir = r"C:\Users\anils"
exclude_dirs = {".git", "node_modules", "AppData", "Local", "Temp", "3D Objects", "Searches", "Contacts", "Links"}

found = []
for root, dirs, files in os.walk(search_dir):
    dirs[:] = [d for d in dirs if d not in exclude_dirs]
    for file in files:
        if file.endswith((".h", ".ino", ".cpp", ".c", ".txt", ".md")):
            file_path = os.path.join(root, file)
            try:
                with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                    if "APID_HANDSHAKE" in content or "APID_LDR_DATA" in content:
                        found.append((file_path, "Found keyword"))
            except Exception:
                pass

for path, msg in found:
    print(f"{path}: {msg}")
