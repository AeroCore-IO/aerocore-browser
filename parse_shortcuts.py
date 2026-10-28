import sys

def parse_vdf(filename):
    with open(filename, 'rb') as f:
        data = f.read()
    
    # Very basic search for "appname", "exe", and "StartDir" in the binary blob
    # to extract relevant Aerocore entries.
    entries = []
    
    # We'll look for identifiers and scan forward for null-terminated strings.
    # This is a bit crude but works for inspection.
    search_terms = [b'appname', b'exe', b'StartDir']
    
    # Find launcher or executable names in the Steam shortcuts database.
    targets = [b'run-aerocore-browser.sh', b'aerocore-browser', b'run.sh']
    
    for target in targets:
        pos = 0
        while True:
            pos = data.find(target, pos)
            if pos == -1: break
            
            # Found a target, let's look around for its AppName, Exe, and StartDir
            # In VDF, AppName is usually followed by the value.
            # We'll just extract a chunk around it for inspection.
            start = max(0, pos - 200)
            end = min(len(data), pos + 200)
            chunk = data[start:end]
            entries.append(chunk)
            pos += len(target)
            
    return entries

filename = '/home/bazzite/.steam/steam/userdata/383524791/config/shortcuts.vdf'
chunks = parse_vdf(filename)
for i, chunk in enumerate(chunks):
    print(f"--- Entry Chunk {i} ---")
    print(chunk.replace(b'\x00', b' [NULL] ').decode('ascii', errors='replace'))
