import metadata_parser as _parser


def get_metadata(file_path):
    return _parser.read_metadata(file_path)
