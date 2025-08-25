console.log('Hello from Webserv!');

document.addEventListener('DOMContentLoaded', function() {

    const heading = document.querySelector('h1');
    if (heading) {
        heading.addEventListener('click', function() {
            alert('WebServ is working correctly with JavaScript!');
            this.style.color = '#' + Math.floor(Math.random()*16777215).toString(16);
        });
    }
    
    const inputs = document.querySelectorAll('input[type="text"], input[type="email"], textarea');
    inputs.forEach(input => {
        const defaultValue = input.value;
        
        input.addEventListener('focus', function() {
            if (this.value === defaultValue) {
                this.value = '';
            }
        });
        
        input.addEventListener('blur', function() {
            if (this.value === '') {
                this.value = defaultValue;
            }
        });
    });
});