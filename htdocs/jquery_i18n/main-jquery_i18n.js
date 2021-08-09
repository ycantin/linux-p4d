jQuery(document).ready(function() {
    
  var update_texts = function() {
    $('body').i18n()
    $('#messages').text($.i18n('message_from', 'Ann', 2, 'female'));
  };
  
  $.i18n().load({
    'en': {
        'welcome': 'Welcome!',
        'message_from': '$1 has send you $2 {{plural:$2|message|messages}}. {{gender:$3|He|She}} is waiting for your response!'
    },
    'ru': {
        'welcome': 'Добро пожаловать!',
        'message_from': '$1 {{gender:$3|отправил|отправила}} вам $2 {{plural:$2|сообщение|сообщения|сообщений}}. {{gender:$3|Он|Она}} ждёт вашего ответа!'
    }
  });
  
  update_texts();
  
  $('.lang-switch').click(function(e) {
    e.preventDefault();
    $.i18n().locale = $(this).data('locale');
    update_texts();
  });

});


