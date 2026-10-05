import subprocess


def _git(directory, *args, binary_output=False):
    result = subprocess.run(["git", "-C", directory, *args], capture_output=True, check=True, text=not binary_output)
    return result.stdout if binary_output else result.stdout.strip()


class Repo:
    def __init__(self, any_directory_inside):
        self.root = _git(any_directory_inside, "rev-parse", "--show-toplevel")

    def short_revision(self, rev="HEAD"):
        return _git(self.root, "rev-parse", "--short", rev)

    def has_uncommitted_changes(self):
        return bool(_git(self.root, "status", "--porcelain", "--untracked-files=no"))

    def working_tree_label(self):
        return self.short_revision() + ("-dirty" if self.has_uncommitted_changes() else "")

    def export_revision(self, rev, target_dir):
        archive = _git(self.root, "archive", rev, binary_output=True)
        subprocess.run(["tar", "-x", "-C", target_dir], input=archive, check=True)


def working_tree_label_or_unknown(directory):
    try:
        return Repo(directory).working_tree_label()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"
