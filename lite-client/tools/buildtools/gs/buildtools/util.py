# Copyright (c) 2013-2017 GomSpace A/S. All rights reserved.

import os
import inspect
import subprocess
import re
import sys
import time
import pprint
import errno
import shutil
import glob
import json
from . import compiler_settings
# Extended buildtools
try:
    import docker_functions
except ImportError:
    docker_functions = None


datatypes = {"uint8": ["UINT8", 1, "uint8_t"],
             "uint16": ["UINT16", 2, "uint16_t"],
             "uint32": ["UINT32", 4, "uint32_t"],
             "uint64": ["UINT64", 8, "uint64_t"],
             "int8": ["INT8", 1, "int8_t"],
             "int16": ["INT16", 2, "int16_t"],
             "int32": ["INT32", 4, "int32_t"],
             "int64": ["INT64", 8, "int64_t"],
             "x8": ["X8", 1, "uint8_t"],
             "x16": ["X16", 2, "uint16_t"],
             "x32": ["X32", 4, "uint32_t"],
             "x64": ["X64", 8, "uint64_t"],
             "double": ["DOUBLE", 8, "double"],
             "float": ["FLOAT", 4, "float"],
             "string": ["STRING", -1, "char"],
             "data": ["DATA", -1, "uint8_t"],
             "bool": ["BOOL", 1, "bool"]}

git_present_cache = [False, False]  # [is tested, is present]


def sizeof(type):
    return datatypes[type][1]


def mkdirs(path, mode=0o777):
    try:
        os.makedirs(path)
    except OSError as e:
        # be happy if someone already created the path
        if e.errno != os.errno.EEXIST:
            raise


def file_callee():
    for frame in inspect.stack():
        file = inspect.getfile(frame[0])
        if "buildtools" not in file:
            caller = inspect.getframeinfo(frame[0])
            return caller.filename, caller.lineno
    return ".", 0


def dir_callee():
    for frame in inspect.stack():
        file = inspect.getfile(frame[0])
        if "buildtools" not in file:
            return os.path.dirname(file)
    return "."


def byteify(input):
    if isinstance(input, dict):
        return {byteify(key): byteify(value)
                for key, value in input.iteritems()}
    elif isinstance(input, list):
        return [byteify(element) for element in input]
    elif isinstance(input, unicode):
        return input.encode('utf-8')
    else:
        return input


def get_iso8601(seconds=None):
    """
    Returns ISO8601 timestamps.
    """
    if seconds is None:
        seconds = time.time()
    return (time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(seconds)),
            time.strftime("%Y-%m-%d", time.gmtime(seconds)),
            time.strftime("%H:%M:%SZ", time.gmtime(seconds)))


def git_present():
    if git_present_cache[0]:
        return git_present_cache[1]
    git_present_cache[0] = True  # Is tested
    try:
        output = subprocess.check_output(['git', 'remote', '-v'], stderr=subprocess.STDOUT)
        if 'github.com' in output.lower() and 'gomspace' in output.lower():
            git_present_cache[1] = True
            return True
    except Exception:
        pass
    git_present_cache[1] = False
    return False


def list_append_unique(list, elm):
    if elm not in list:
        list += [elm]


def get_relative_git_path():
    proj_path = get_project_root()
    path = os.getcwd()
    if path == proj_path:
        return os.path.basename(path)
    path = re.sub(r'^' + proj_path + '/', '', path)
    return path


def get_git_info(tag=None, forcedTag=None, extendedInfo=False, gitTagPattern=None):
    """
    Return GiT information as a dictionary
    """
    # get GiT information and validate
    if forcedTag:
        tag = forcedTag
    elif git_present():
        cmd = ['git', 'describe', '--long', '--always', '--dirty=+']
        if gitTagPattern is not None:
            cmd += ["--match", gitTagPattern]
        tag = subprocess.check_output(cmd).strip()
    elif tag:
        pass
    else:
        raise Exception("GiT is not present and no tag/forcedTag provided")

    variant = None
    revision = None
    elms = tag.split('/')
    if len(elms) == 1:
        revision = elms[0]
    if len(elms) >= 2:
        variant = elms[0]
        revision = elms[1]

    if variant and re.search(r'[^a-zA-Z0-9-]', variant):
        raise Exception("Invalid variant [%s] in tag [%s]" % (variant, tag))

    revision = re.sub(r'^v', '', revision)  # remove legacy leading 'v'

    info = {}
    info['revision'] = revision
    info['variant'] = variant

    version = None
    version_only = None
    dirty = False
    shortcommit = None

    # version tag in revision: <version>-<commits_after>-g<shortcommit>[dirty]
    m = re.search(r'^([0-9]+(?:\.[0-9]+)+)-([0-9]+)-g([a-f0-9]+)(\+)?$', revision)
    if m and m.lastindex >= 3:
        version_only = m.group(1)
        shortcommit = m.group(3)
        if m.lastindex >= 4:
            dirty = True
        if int(m.group(2)) or dirty:
            version = revision  # none-tagged release, use revision
        else:
            version = version_only  # only use version number

    else:
        m = re.search(r'^([a-f0-9]+)(\+)?$', revision)
        if m and m.lastindex >= 1:
            # no version tag - just short commit
            shortcommit = m.group(1)
            if m.lastindex >= 2:
                dirty = True
        else:
            m = re.search(r'^([0-9]+(?:\.[0-9]+)+)?$', revision)
            if m and m.lastindex >= 1:
                version = version_only = revision
            else:
                raise Exception("Invalid tag [%s]" % (tag))

    info['version'] = version
    info['version_only'] = version_only
    info['dirty'] = dirty
    info['shortcommit'] = shortcommit
    if info['version_only']:
        if len(info['version_only'].split('.')) == 3:
            info['major'], info['minor'], info['fix'] = info['version_only'].split('.')

    # if tag provided (usually written in wscript), we should validate against GiT
    # - but tag will soon be removed, so we skip

    if extendedInfo:

        commit = None
        tags = []
        branches = []

        cmd = ['git', 'rev-parse', 'HEAD']
        commit = subprocess.check_output(cmd).strip()
        if not re.match(r'^[a-f0-9]+$', commit):
            raise Exception("Invalid commit [%s]" % (commit))
        if not commit.startswith(shortcommit):
            raise Exception("Mismatch in commit SHAs [%s] [%s]" % (commit, shortcommit))

        info['commit'] = commit
        info['path'] = get_relative_git_path()

        cmd = ['git', 'branch', '-a', '--contains', commit]
        out = subprocess.check_output(cmd).strip()
        pattern = re.compile(r'^\s*\*?\s*([^()/\n]+)$', re.MULTILINE)
        for m in re.finditer(pattern, out):
            list_append_unique(branches, m.group(1).strip())
        pattern = re.compile(r'^.+/([^/\n].+)$', re.MULTILINE)
        for m in re.finditer(pattern, out):
            list_append_unique(branches, m.group(1).strip())

        cmd = ['git', 'tag', '--contains', commit]
        out = subprocess.check_output(cmd).strip()
        pattern = re.compile(r'^(.+)', re.IGNORECASE | re.MULTILINE)
        for m in re.finditer(pattern, out):
            list_append_unique(tags, m.group(1))

        info['tags'] = tags
        info['branches'] = branches

        cmd = 'git remote -v'.split()
        output = subprocess.check_output(cmd)
        repo_name_pat = re.compile(r'/([a-zA-Z0-9_\-]+)')
        url_pat = re.compile(r'((git@|https)[a-zA-Z0-9\-_./:]+)')

        for l in output.split('\n'):
            m = repo_name_pat.findall(l)
            if len(m) > 0:
                info['repo_name'] = m[-1]
            m = url_pat.search(l)
            if m:
                info['url'] = m.group(0)

        # Tags on current branch
        cmd = ['git', 'tag', '--merged', 'HEAD']
        info['branch_tags'] = subprocess.check_output(cmd).strip().split('\n')

    return info


class build_info:
    def __init__(self, info={}):
        self.__info = info

    def __str__(self):
        str = pprint.pformat(self.__info) + "\n"
        str += "doc revision     : " + self.doc_revision() + "\n"
        str += "filename revision: " + self.filename_revision() + "\n"
        str += "model(<model>)   : " + self.model("<model>")
        return str

    def value(self, key):
        if (key in self.__info):
            return self.__info[key]
        return None

    def info(self):
        return self.__info

    def variant(self):
        return self.value('variant')

    def revision(self):
        return self.value('revision')

    def doc_revision(self):
        ret = ''
        variant = self.variant()
        if variant:
            ret += variant + '-'
        ret += self.__info['version']
        return ret

    def filename_revision(self):
        return self.doc_revision()

    def model(self, model):
        variant = self.variant()
        if variant:
            model += '-' + variant
        return model


def get_build_info(appName=None, tag=None, seconds=None, forcedTag=None, writeToArtifacts=False, gitTagPattern=None):
    """
    Return build information, e.g. timestamps, revision, etc.
    """
    info = get_git_info(tag, forcedTag, gitTagPattern=gitTagPattern)
    if info['version'] is None:
        raise Exception("No tag found in github repository - it is needed in get_build_info (e.g., in doc)")
    (info['timestamp'], info['date'], info['time']) = get_iso8601(seconds)
    info['time_csp'] = re.sub(r'Z$', '', info['time'])
    if appName:
        info['appname'] = appName
    if writeToArtifacts:
        filename = os.path.join(artifact_dir_name(), "build_info.json")
        create_dir_for_filename(filename)
        with open(filename, 'w') as f:
            json.dump(info, f, indent=4)
    return build_info(info)


def docker_shell(dir, docker_image):
    args = ["/bin/bash"]
    docker_args = docker_functions.get_default_args(dir)
    docker_args += docker_functions.get_image_specific_args(docker_image, dir)
    args = ['docker', 'run', '-i'] + docker_args.split() + [docker_image] + args
    cmd = " ".join(args)
    print('DOCKER SHELL: dir: [%s]' % (str(dir)))
    print(cmd)
    os.system(cmd)


def flush():
    sys.stdout.flush()
    sys.stderr.flush()


def waf_command_exec(cmd, options, dir, force_docker_img):
    args = ['python', 'waf'] + cmd + options
    if docker_functions:
        docker_image = None
        docker_args = None
        if force_docker_img is not None:
            docker_image = force_docker_img
            docker_args = docker_functions.get_default_args(dir)
            docker_args += docker_functions.get_image_specific_args(docker_image, dir)
        elif '--docker' in sys.argv:
            (docker_image, docker_args) = docker_functions.get_docker_params(options, dir)
            docker_args += docker_functions.get_image_specific_args(docker_image, dir)

        if docker_image is not None and docker_args is not None:
            args = ['docker', 'run'] + docker_args.split() + [docker_image] + args

    print('WAF COMMAND: %s, dir: [%s]' % (str(args), str(dir)))
    print(' '.join(args))

    flush()
    exit_code = subprocess.Popen(args, cwd=dir).wait()
    flush()
    if exit_code:
        raise Exception("Command: [%s] failed, dir: [%s], exit code: %s" % (args, dir, str(exit_code)))


def waf_command(options=[],
                cmd=['distclean', 'configure', 'clean', 'build'],
                dir='.',
                recurse=False,
                force_docker_img=None):
    """
    Run a ``waf`` command

    :param options: a list of command line options passed to ``waf``
    :param cmd: a list of subcommands to run with ``waf``, useful when wanting to build ``doc`` or ``gs_dist``
    :param dir: the working directory for the ``waf`` command.
    :param recurse: If True run the ``waf`` command in all subfolder with a wscript
    :param force_docker_img: If set the waf command will be executed using the specified docker image
    """
    if recurse:
        for d in os.listdir(dir):
            tgt_dir = os.path.join(dir, d)
            if os.path.isfile(os.path.join(tgt_dir, 'wscript')):
                waf_command_exec(cmd, options, dir=tgt_dir, force_docker_img=force_docker_img)
    else:
        waf_command_exec(cmd, options, dir=dir, force_docker_img=force_docker_img)


def buildtools_root():
    return os.environ['GS_BUILDTOOLS']


def buildtools_cmd():
    script = 'gsbuildtools.py'
    cmd = os.path.abspath(os.path.join('.', script))
    if not os.path.exists(cmd):
        cmd = os.path.join(buildtools_root(), script)
    return cmd


def is_project_root(dir):
    """
    Returns True if directory is project root.
    NOTE: This only works, if gsbuildtools_bootstrap.py has been run from project root.
    """
    if dir:
        path = os.path.join(dir, '.waf_gs_project_root')
        if os.path.isfile(path):
            return True
    return False


def get_project_root():
    """
    Returns project root directory
    NOTE: This only works, if gsbuildtools_bootstrap.py has been run from project root.
    """
    path = os.getcwd()
    while path:
        if is_project_root(path):
            return path
        (path, tail) = os.path.split(path)
        if not path or not tail:
            break

    raise Exception("get_project_root() failed, could not find root above [%s] "
                    "- please run gsbuildtools_bootstrap.py from root" % (dir))


def is_component_root(dir):
    """
    Returns True if directory is component root.
    NOTE: This only works with GIT and sub-modules.
    """
    if dir:
        path = os.path.join(dir, '.git')
        if os.path.isdir(path) or os.path.isfile(path):
            return True
        return False
    return True


def buildtools_command(options=['-h']):
    cmd = [buildtools_cmd()] + options
    flush()
    subprocess.check_call(cmd)
    flush()


def artifact_dir_name(subdir=None):
    dir = 'artifacts'
    if subdir:
        dir = os.path.join(dir, subdir)
    return dir


def clear_artifacts():
    dir = artifact_dir_name()
    if os.path.exists(dir):
        entries = os.listdir(dir)
        for e in entries:
            if e in ['version.txt']:  # buildtools generaeted files
                continue
            path = os.path.join(dir, e)
            if os.path.isdir(path):
                shutil.rmtree(path)
            else:
                os.remove(path)


def create_dir_for_filename(filename):
    mkdirs(os.path.dirname(filename))


def copy_to_artifact(files, subdir=None):
    dir = os.path.join(artifact_dir_name(subdir))
    for i in files:
        for file in glob.glob(r'' + i):
            if os.path.isdir(file):
                print('Copying directory: ' + file + ' -> ' + dir + ' ...')
                shutil.copytree(file, dir)
            else:
                mkdirs(dir)
                print('Copying file: ' + file + ' -> ' + dir + ' ...')
                shutil.copy(file, dir)


def get_branch_type(branch):
    if branch and (len(branch) > 0):
        if branch == 'master':
            return 'master'
        if re.match(r'^release-.*$', branch):
            return 'release'
        return 'develop'
    return None


def get_best_branch(branch_list):
    if 'master' in branch_list:
        return 'master'
    for branch in branch_list:
        if get_branch_type(branch) == 'release':
            return branch
    if 'develop' in branch_list:
        return 'develop'
    # When in doubt no branches are returned.
    # ie. it will newer resolve to anything that is not master, develop or release-x.x
    return ''


def get_best_branch_with_tag(tag, repo_path):
    cmd = "git branch --contains %s -a" % (tag)
    output = subprocess.check_output(cmd.split(" "), cwd=repo_path)
    branches = []
    for line in output.split("\n"):
        line_spl = line.split("/")
        if len(line_spl) == 3:
            branches.append((line_spl[2]).strip())
    return get_best_branch(branches)


def get_branches(path, contains=None):
    cmd = 'git branch -a'
    if contains:
        cmd += ' --contains %s' % contains
    output = subprocess.check_output(cmd.split(), cwd=path)
    branch_pat = re.compile(r'[a-zA-Z]+/[a-zA-Z]+/([a-zA-Z0-9\-_.]+)')
    branches = []
    for l in output.split('\n'):
        m = branch_pat.search(l)
        if m:
            b = m.group(1)
            if b != 'HEAD':
                branches.append(b)
    return branches


def get_current_sha(path):
    cmd = 'git rev-parse HEAD'
    return subprocess.check_output(cmd.split(), cwd=path).strip()


def get_current_branch(path='.'):
    cmd = 'git status -b --porcelain'
    pat = re.compile(r'^(.*/)?([0-9a-zA-Z\-\.]+)')
    output = subprocess.check_output(cmd.split())
    m = pat.search(output)
    if m:
        return m.group(2)
    elif 'GIT_BRANCH' in os.environ:
        m = pat.search(os.environ['GIT_BRANCH'])
        if m:
            return m.group(2)
        else:
            return None
    else:
        return None


def client_gsbuildall(dir='client'):
    cmd = ['python', 'gsbuildall.py']
    flush()
    exit_code = subprocess.Popen(cmd, cwd=dir).wait()
    flush()
    if exit_code:
        raise Exception("Failed to execute: %s, dir=%s, result=%d" % (str(cmd), dir, exit_code))


def return_latest_if_present(url, version):
    cmd = ['git', 'ls-remote', '--tags', url]
    output = subprocess.check_output(cmd)
    for line in output.split('\n'):
        if 'refs/tags/' in line and '^{}' not in line:
            tag = line.split("\t")[1].replace('refs/tags/', '')
            if version in tag and '-latest' in tag:
                return tag
    return version


def move_tag(tagname):
    try:
        # Delete the tag local tag
        cmd = 'git tag -d ' + tagname
        subprocess.check_call(cmd.split())
    except subprocess.CalledProcessError:
        pass

    # Add the tag local (not annotated)
    cmd = 'git tag ' + tagname
    subprocess.check_output(cmd.split())

    # Push the local tag
    cmd = 'git push origin ' + tagname + ' --force'
    subprocess.check_output(cmd.split())


def manifest_command(exclude=[], subdir=None):
    cmd = ['gitinfo', '-r', '--manifest']
    if subdir:
        cmd += ['--manifest-subdir=' + subdir]
    if len(exclude):
        cmd += ['--exclude'] + exclude
    buildtools_command(cmd)


def status_command(skip_verify=[], verbose=False):
    import gs.buildtools.status
    gs.buildtools.status.status_command(skip_verify=skip_verify, verbose=verbose)


def cppcheck_command(options=['--enable=all',
                              '--inconclusive',
                              '--std=posix',
                              '--std=c99',
                              '--inline-suppr'],
                     sources=['.'],
                     suppression_files=[],
                     output_file='build/cppcheck-results.xml',
                     excludes=[],
                     basic_excludes=['./clients', './lib']):
    import gs.buildtools.check
    gs.buildtools.check.cppcheck_command(options=options, sources=sources, suppression_files=suppression_files,
                                         output_file=output_file, excludes=excludes, basic_excludes=basic_excludes)


def stylecheck_command(sources=['src/**/*.[c,h]*', 'include/**/*.h*', 'tst/**/*.[h,c]*'],
                       profile='gomspace_basic', output_file=None,
                       excludes=[],
                       basic_excludes=['lib/**/*', 'clients/**/*', '**/.waf*/**/*', '**/build/**/*',
                                       '**/tools/buildtools/**/*']):
    import gs.buildtools.check
    gs.buildtools.check.stylecheck_command(sources=sources, profile=profile, output_file=output_file,
                                           excludes=excludes, basic_excludes=basic_excludes)


def get_existing_modules(search=True, search_folder=None):
    try:
        import gs.buildtools.dependencies
        return gs.buildtools.dependencies.get_existing_modules(search_folder=search_folder)
    except ImportError:
        return []
