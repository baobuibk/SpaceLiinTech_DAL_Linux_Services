# Copyright (c) 2013-2017 GomSpace A/S. All rights reserved.
"""
Load and parse compiler settings from JSON.
"""

import json
import os
import pprint

compiler_settings = None

validWarningLevels = {'strict', 'relaxed'}


def keyValueMatch(set, key, value):
    if value and (key in set) and (set[key] == value):
        return True
    return False


def hasKey(set, key):
    if (key in set) and set[key]:
        return True
    return False


def byteify(input):
    return input

# extend key, e.g. key += key_suffix
def extend_values(set, suffix):
    suffix = '_' + suffix
    for key in set:
        if key.endswith(suffix):
            base_key = key.replace(suffix, "")
            # print "found key: " + key + ", base_key: " + base_key
            if not hasKey(set, base_key):
                raise KeyError('Missing base key [%s] for [%s]' % (base_key, key))
            set[base_key] += set[key]


def load(file="compiler_settings.json"):
    global compiler_settings
    if not compiler_settings:
        absfile = os.path.join(os.path.dirname(__file__), file)
        # print "Loading compiler settings: " + absfile
        settings = None
        with open(absfile) as ifile:
            settings = json.load(ifile)
            settings = byteify(settings)  # some of Waf python doesn't like unicode for compiler options
        compiler_settings = settings
    return compiler_settings


def get(part=None, arch=None, mcu=None, warninglevel="relaxed", build='release'):

    if warninglevel not in validWarningLevels:
        raise KeyError('Invalid warninglevel=[%s], valid levels=[%s]' %
                       (str(warninglevel), ', '.join(sorted(validWarningLevels, key=str.lower))))

    settings = load()
    sets = dict()
    match = None

    for set in settings['compiler_settings']:

        if 'name' not in set:
            raise KeyError('Missing key=[name] in: ' + pprint.pformat(set))

        name = set['name']
        if name in sets:
            raise KeyError('Duplicate set: ' + pprint.pformat(set))

        sets[name] = set

        if keyValueMatch(set, 'gs_part', part):
            match = name
        elif keyValueMatch(set, 'gs_mcu', mcu):
            match = name
        elif (not match) and keyValueMatch(set, 'gs_arch', arch):
            match = name

    if match is None:
        raise LookupError("Failed to find compiler settings for: part=[%s], arch[%s]" % (str(part), str(arch)))

    set = sets[match]

    extend_values(set, build.upper())
    extend_values(set, warninglevel)

    for key in ['CFLAGS', 'CXXFLAGS']:
        if hasKey(set, key):
            wkey = 'warnings'
            if hasKey(set, wkey):
                set[key] += set[wkey]

    return set


def _include_part(set, keywords, excludes):
    yes = False
    no = False
    for key, value in set.items():
        if not isinstance(value, list):
            value = value.split(' ')

        for label in excludes:
            if label in value:
                no = True
                break

        if len(keywords) == 0:
            yes = True
            continue

        for label in keywords:
            if label in value:
                yes = True

    return ((not no) and (yes))


def get_parts(keywords=[], excludes=[]):
    """
    Return parts matching the specified keywords
    """
    if len(keywords) == 0:
        keywords = ['main_archs']
    settings = load()
    parts = []
    for set in settings['compiler_settings']:
        if 'gs_part' not in set:
            continue

        part = set['gs_part']
        if _include_part(set, keywords, excludes):
            if part not in parts:  # keep order
                parts.append(part)
        else:
            if part in parts:
                parts.remove(part)

    return list(parts)


def get_container_settings_from_part(part):
    settings = load()

    for s in settings['compiler_settings']:
        if s['gs_part'] == part:
            if 'docker-container' in s.keys() and 'docker-args' in s.keys():
                return (s['docker-container'], s['docker-args'])
            else:
                return (None, None)
    return (None, None)
