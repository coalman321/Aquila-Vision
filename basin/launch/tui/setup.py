from setuptools import find_packages, setup

package_name = 'launch_tui'

setup(
    name=package_name,
    version='1.0.1',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/css', ['layout.tcss'])
    ],
    install_requires=['setuptools', 'textual>=0.11.0'],
    zip_safe=True,
    maintainer='coalman321',
    maintainer_email='cjtucker321@gmail.com',
    description='TODO: Package description',
    license='TODO: License declaration',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'launch_tui = launch_tui.launch_tui:main'
        ],
    },
)
