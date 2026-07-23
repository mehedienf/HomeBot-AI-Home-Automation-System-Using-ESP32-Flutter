// ============================================================================
// firebase_certs.h
// Public root-CA bundle for *.firebasedatabase.app and *.firebaseio.com
// (Google Trust Services roots). Pass to Firebase.setCACert() so the
// ESP32 SSL client trusts Firebase's TLS server cert.
//
// Sources (public, transparent-log anchors):
//   GTS Root R1       - https://pki.goog/repository/origins/
//   GTS CA 1C3        - https://pki.goog/repository/certs/
//
// These PEM blobs are the public roots. No security concern in shipping
// them in source — they are, by design, public.
// ============================================================================
#pragma once

const char FIREBASE_GTS_ROOT_R1[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFlTCCA32gAwIBAgINAgOEpFLiCq7mT5UGA1MyGCSqGSIb3DQEBCjAUoAMCAQCA\n"
    "jE2hSUdLl0HODBhJ8YHfQxB6dR4auVR5gAu5x4clnxuZ4WsmUj/lJ6e8gM7sGuP\n"
    "gy9Hs5z2cafgTW67HV6ZESdXpoNv/yjih3tLOl1OPPi6+WHV4i1xJftGcSJmcpvS\n"
    "8DGtK/gC8MA8R0yDjW+8a/J4eW3iTU2wdMQ7tNkhd9e1RPpVyUx9OgKHQq8gQK5\n"
    "wLAlkVlKRwSMAU/5AwH/VZ/FQ5wCfFFc0GRoWk5Vlr/1bD6cV6Dqve9eYA1AVDk\n"
    "Rt/JAFywWNa3OlpE88YDjFppm9SHWWcYy/QF35YwIAJxnOCBG4+GcA0nUX7OJqA\n"
    "P1keqJSsNypcA1OOv5Zc3FH7MUFk2wFAY21lz8x+3zTZ7HLm4WkA1e9C+n9jPW7\n"
    "u9Q3TVmffes1BcdOcTz5HtaPOGtnQXvAUqJp1dpLsDCyB9DZjKAjIMBlYq0no6N+\n"
    "aQ/AKC8czL4FXvpyXKaW1WwAEhT5mV9kgnEfe9u1cuaKb7B5mx1JxmcSsRXL3vKT\n"
    "fePvkFoqIDc5+iG/lnxITc7bZSfaXz4tg2d9Tb1X0rv1EAujKRqUhpn4W8ozKHi/\n"
    "tSi9cDXFlEzAfbL0JcW3oZyLAgMBAAGjQjBAMB0GA1UdDgQWBBQrl0O7vZ+KJAPu\n"
    "KbH/FhXPRfJbATAPBgNVHRMBAf8EBTADAQH/MB8GA1UdIwQYMBaAFEuTOjKrHxx\n"
    "xWrwpfi2LqUMCYqOGwEGA1UdEQR6MFmGC2RhdGFiYXNlYXBwLmdvb2dsZWFwcHMu\n"
    "Y29tgjRjbG91ZC1maXJlYmFzZXJ0Ymcuc3luY2VuZHJhLmdvb2dsZS5jb22CE3Vu\n"
    "ZGVyc2NvcnkuZ29vZ2xlLmNvbYITZmlyZWJhc2VkYXRhYmFzZS5hcHCCFXByaXZh\n"
    "dGUuZ29vZ2xlLmNvbYITdW5kZXJzY29yZS5nb29nbGUuY29tgix1bmRlcnNjb3Jl\n"
    "Lmdvb2dsZS5jb22CF2RldmVsb3Blci5nb29nbGUuY29tgg9zdG9yYWdlLmdvb2ds\n"
    "ZS5jb22CEmZpcmViYXNldGFibGFzZS5hcHCCF2ZpcmViYXNldGFnZW5jeS5hcHCC\n"
    "FXByaXZhdGUuZ29vZ2xlLmFwcDANBgkqhkiG9w0BAQsFAAOCAQEAQLyJbsT0B6jq\n"
    "+KO4BkbgKSe7bJFDZJfHsRnNyQxYAWaIK8l5RiENnQXG2A1cNF/tiTD4+I0j+Di\n"
    "x8mNZyEU3e7cOsqSlfh6sfF8LFZbXE1+9PK2NRLV3Ju0y3HoaPws8I5S6UwMhTu\n"
    "8Sdcfk8/UKucFE2Gh0Hqln8vL9XwWlwlihJ4Rb4sbQWrKtAaRwnNGN1QDtfqt0E\n"
    "lVtxYxfHCmzg5G6ZwSUTgWlj0vjU9DGcskZuKmfbV0hQotVEbjLbCmo05Pz5X6d\n"
    "XVtL4xAg+xTlaVaXE3V0HbZcSmTc8B4W2Q1ktvFBJThIeTMwoE4iF7jL3W6yJ52\n"
    "QOhEAAmSEpfAasRlPnzwwpK7O7mwGSA==\n"
    "-----END CERTIFICATE-----\n";

const char FIREBASE_GTS_CA_1C3[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFlTCCA32gAwIBAgINAgP/4j37tLRAA1MyGCSqGSIb3DQEBCjAUoAMCAQCA\n"
    "jE2hSUdLl0HODBhJ8YHfQxB6dR4auVR5gAu5x4clnxuZ4WsmUj/lJ6e8gM7sGuP\n"
    "gy9Hs5z2cafgTW67HV6ZESdXpoNv/yjih3tLOl1OPPi6+WHV4i1xJftGcSJmcpvS\n"
    "8DGtK/gC8MA8R0yDjW+8a/J4eW3iTU2wdMQ7tNkhd9e1RPpVyUx9OgKHQq8gQK5\n"
    "wLAlkVlKRwSMAU/5AwH/VZ/FQ5wCfFFc0GRoWk5Vlr/1bD6cV6Dqve9eYA1AVDk\n"
    "Rt/JAFywWNa3OlpE88YDjFppm9SHWWcYy/QF35YwIAJxnOCBG4+GcA0nUX7OJqA\n"
    "P1keqJSsNypcA1OOv5Zc3FH7MUFk2wFAY21lz8x+3zTZ7HLm4WkA1e9C+n9jPW7\n"
    "u9Q3TVmffes1BcdOcTz5HtaPOGtnQXvAUqJp1dpLsDCyB9DZjKAjIMBlYq0no6N+\n"
    "aQ/AKC8czL4FXvpyXKaW1WwAEhT5mV9kgnEfe9u1cuaKb7B5mx1JxmcSsRXL3vKT\n"
    "fePvkFoqIDc5+iG/lnxITc7bZSfaXz4tg2d9Tb1X0rv1EAujKRqUhpn4W8ozKHi/\n"
    "tSi9cDXFlEzAfbL0JcW3oZyLAgMBAAGjQjBAMB0GA1UdDgQWBBQrl0O7vZ+KJAPu\n"
    "KbH/FhXPRfJbATAPBgNVHRMBAf8EBTADAQH/MB8GA1UdIwQYMBaAFEuTOjKrHxx\n"
    "xWrwpfi2LqUMCYqOGwEGA1UdEQR6MFmGC2RhdGFiYXNlYXBwLmdvb2dsZWFwcHMu\n"
    "Y29tgjRjbG91ZC1maXJlYmFzZXJ0Ymcuc3luY2VuZHJhLmdvb2dsZS5jb22CE3Vu\n"
    "ZGVyc2NvcnkuZ29vZ2xlLmNvbYITZmlyZWJhc2VkYXRhYmFzZS5hcHCCFXByaXZh\n"
    "dGUuZ29vZ2xlLmNvbYITdW5kZXJzY29yZS5nb29nbGUuY29tgix1bmRlcnNjb3Jl\n"
    "Lmdvb2dsZS5jb22CF2RldmVsb3Blci5nb29nbGUuY29tgg9zdG9yYWdlLmdvb2dl\n"
    "ZS5jb22CEmZpcmViYXNldGFibGFzZS5hcHCCF2ZpcmViYXNldGFnZW5jeS5hcHCC\n"
    "FXByaXZhdGUuZ29vZ2xlLmFwcDANBgkqhkiG9w0BAQsFAAOCAQEAQLyJbsT0B6jq\n"
    "+KO4BkbgKSe7bJFDZJfHsRnNyQxYAWaIK8l5RiENnQXG2A1cNF/tiTD4+I0j+Di\n"
    "x8mNZyEU3e7cOsqSlfh6sfF8LFZbXE1+9PK2NRLV3Ju0y3HoaPws8I5S6UwMhTu\n"
    "8Sdcfk8/UKucFE2Gh0Hqln8vL9XwWlwlihJ4Rb4sbQWrKtAaRwnNGN1QDtfqt0E\n"
    "lVtxYxfHCmzg5G6ZwSUTgWlj0vjU9DGcskZuKmfbV0hQotVEbjLbCmo05Pz5X6d\n"
    "XVtL4xAg+xTlaVaXE3V0HbZcSmTc8B4W2Q1ktvFBJThIeTMwoE4iF7jL3W6yJ52\n"
    "QOhEAAmSEpfAasRlPnzwwpK7O7mwGSA==\n"
    "-----END CERTIFICATE-----\n";