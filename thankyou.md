---
layout: default
---

{% raw %}
<script>
    window.onload = function() {
        $.urlParam = function(name){
            var results = new RegExp('[\\?&]' + name + '=([^&#]*)').exec(window.location.href);
            if (results==null){
            return null;
            }else{
            return results[1] || 0;
            }
        }
         setTimeout(function() {
            window.location = 'https://github.com/desowin/usbpcap/releases/download/'+$.urlParam('file');
        }, 2000);
    }
</script>
{% endraw %}

Thank you for downloading USBPcap
---------------------------------

Your download should start shortly. If it does not start please click [here](https://github.com/desowin/usbpcap/releases/download/1.4.1.0/USBPcapSetup-1.4.1.0.exe)

MSDN Subscription
-----------------

Are you MVP and/or have free gift MSDN subscription available? If you don't know who to give it to, please consider me as a receiver and contact me at [desowin@gmail.com](mailto:desowin@gmail.com).

USBPcap needs your help!
------------------------

This project was developed as part of master's thesis. See [donors](donors.html) page for non-development help.

Other ways to help include, but are not limited to, following:  

*   Writing documentation (perhaps chm file) that would be bundled with installer.
*   Fixing typos/grammar errors on USBPcap website.
*   Translating webpage into foreign languages.
