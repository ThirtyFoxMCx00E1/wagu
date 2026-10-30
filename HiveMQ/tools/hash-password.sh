#!/usr/bin/env bash
# Hash a password for credentials.xml:  sudo ./hash-password.sh 'new password'
exec /opt/mp/jdk/bin/java -jar /opt/mp/hivemq/extensions/hivemq-file-rbac-extension/hivemq-file-rbac-extension-*.jar -p "$1" -q
