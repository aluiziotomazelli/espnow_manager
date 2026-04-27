import os
import re

for filename in os.listdir("host_test/common/"):
    if filename.startswith("mock_") and filename.endswith(".hpp"):
        path = os.path.join("host_test/common/", filename)
        with open(path, "r") as f:
            content = f.read()
        
        # Wrap content in namespace espnow
        if "namespace espnow" not in content:
            new_content = "namespace espnow {\n\n" + content + "\n\n} // namespace espnow"
            with open(path, "w") as f:
                f.write(new_content)
