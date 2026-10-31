def pytest_configure(config):
    config.addinivalue_line("markers", "slow: sanitizer builds (several minutes)")
