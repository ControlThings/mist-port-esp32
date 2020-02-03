# Information about this repository

This repository has been formed from the original mist-port-esp32 repository at ControlThings Oy Ab using the following commands:

```sh
git clone --single-branch -b v1.0.0-release foremost.controlthings.fi:/ct/mist/mist-port-esp32 mist-port-esp32-apache --depth 1
echo ba8e7212198afda5bbfd64c6ee783485b8ecb67c  >.git/info/grafts
git filter-branch -- --all
git remote remove origin
#Check that there are not other remotes
git prune
git gc --aggressive
```
