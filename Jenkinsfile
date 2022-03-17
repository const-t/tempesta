pipeline {
    agent any 
    stages {
        stage('Example Build') {
            agent { any } 
            steps {
                echo 'Hello, Jenkins'
                sh 'uname -r'
            }
        }
        stage('Example Test') {
            agent { any } 
            steps {
                echo 'Python Version'
                sh 'python3 -V'
            }
        }
    }
}