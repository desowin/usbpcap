Following certificates were created with following commands:
MakeCert -r -pe -ss PrivateCertStore -n "CN=USBPcapTest" -sv USBPcapTestCert.pvk USBPcapTestCert.cer
pvk2pfx -pvk USBPcapTestCert.pvk -spc USBPcapTestCert.cer -pfx USBPcapTestCert.pfx

Password: USBPcap


WARNING: Prior to installing the test-signed driver you need to run:
CertMgr /add USBPcapTestCert.cer /s /r localMachine root
CertMgr /add USBPcapTestCert.cer /s /r localMachine trustedpublisher
Bcdedit.exe -set TESTSIGNING ON
