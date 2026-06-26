import tarfile
import os

gem_path = r"C:\Users\anils\cosmos\plugins\DEFAULT\openc3-cosmos-shivam\openc3-cosmos-shivam-1.0.0.gem"
extract_dir = r"C:\Users\anils\LoRa_Image_CCSDS\gem_extract"

if not os.path.exists(extract_dir):
    os.makedirs(extract_dir)

# A gem is a tar file containing metadata.gz and data.tar.gz
with tarfile.open(gem_path, 'r') as gem:
    gem.extractall(extract_dir)

# Now extract data.tar.gz
data_tar_path = os.path.join(extract_dir, 'data.tar.gz')
if os.path.exists(data_tar_path):
    with tarfile.open(data_tar_path, 'r:gz') as data_tar:
        data_tar.extractall(extract_dir)
    print("Extraction successful!")
else:
    print("data.tar.gz not found in gem.")
